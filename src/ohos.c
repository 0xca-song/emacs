/* OHOS native bridge: the NAPI-facing side of the backend.

   Mirrors the role of android.c: it is the only place that knows about
   the host callback ABI.  Emacs redisplay must not call the host
   directly; it enqueues drawing commands here, and the ArkUI thread
   drains them from ohos_pump_draw_commands.  */

#include <config.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "lisp.h"
#include "ohosgui.h"

/* A full redraw on a 4K/320-column frame can exceed 30k glyph commands.
   Keep enough headroom that a cold resize does not immediately overflow
   the ring and force an epoch-gap repaint.  */


/* IMAGE commands carry an owned RGBA copy.  A full ring of 4K/ETAF images
   would otherwise pin tens of gigabytes; keep a bounded byte budget and
   drop new images when the UI is too far behind.  */
#define OHOS_IMAGE_BYTES_MAX (64u * 1024u * 1024u)

static unsigned long draw_dropped;

/* Temporary input-to-first-draw probe.  */
static uint64_t probe_pop_ns;
static bool probe_after_pop;
static bool probe_present_pending;
static int probe_logs;
static uint64_t probe_pop_seq;

uint64_t
ohos_probe_pop_ns (void)
{
  return probe_pop_ns;
}

bool
ohos_probe_pending (void)
{
  return probe_present_pending;
}

void
ohos_probe_log (const char *tag)
{
  if (!probe_present_pending)
    return;
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  uint64_t now = (uint64_t) ts.tv_sec * 1000000000ull + ts.tv_nsec;
  char dbg[128];
  snprintf (dbg, sizeof dbg, "%s %.1f us", tag,
            (double) (now - probe_pop_ns) / 1000.0);
  ohos_log (dbg);
}

/* The host API installed by ohos_abi.c before Emacs starts.  NULL only
   during very early startup, before the host calls the run entry.  */
static const struct ohos_host_api *host_api;
static unsigned draw_epoch;

void
ohos_set_host_api (const struct ohos_host_api *api)
{
  host_api = api;
}

const struct ohos_host_api *
ohos_get_host_api (void)
{
  return host_api;
}

void
ohos_log (const char *s)
{
  if (host_api && host_api->frame.log)
    host_api->frame.log (s);
}

unsigned
ohos_draw_epoch (void)
{
  return draw_epoch;
}

unsigned long
ohos_draw_dropped_count (void)
{
  return draw_dropped;
}

/* Number of commands waiting for the UI thread.  The host uses this to
   decide whether to map a NativeWindow buffer before pumping.  */
int
ohos_draw_pending (void)
{
  return 0;
}



static bool
ohos_direct_mode (void)
{
  static int cached = -1;
  if (cached < 0)
    cached = getenv ("EMACS_OHOS_DIRECT") ? 1 : 0;
  return cached != 0;
}

static void
enqueue (const struct ohos_draw_command *cmd)
{
  static struct ohos_draw_command direct_buf[512];
  static int direct_n;

  if (cmd->op == OHOS_OP_IMAGE && cmd->ptr)
    {
      if (direct_n > 0 && host_api && host_api->draw.batch)
        host_api->draw.batch (direct_buf, direct_n);
      direct_n = 0;
      if (host_api && host_api->draw.batch)
        host_api->draw.batch (cmd, 1);
      xfree ((void *) cmd->ptr);
      return;
    }
  if (cmd->op == OHOS_OP_PRESENT)
    {
      if (direct_n > 0 && host_api && host_api->draw.batch)
        host_api->draw.batch (direct_buf, direct_n);
      direct_n = 0;
      if (host_api && host_api->draw.present)
        host_api->draw.present ();
      return;
    }
  if (direct_n == 512)
    {
      if (host_api && host_api->draw.batch)
        host_api->draw.batch (direct_buf, direct_n);
      direct_n = 0;
    }
  direct_buf[direct_n++] = *cmd;
}

void
ohos_draw_clear (uint32_t bg)
{
  struct ohos_draw_command cmd = { .op = OHOS_OP_CLEAR, .bg = bg };
  enqueue (&cmd);
}

void
ohos_draw_fill (int x, int y, int w, int h, uint32_t color)
{
  struct ohos_draw_command cmd = { .op = OHOS_OP_FILL,
			       .x = x, .y = y, .w = w, .h = h,
			       .bg = color };
  enqueue (&cmd);
}

void
ohos_draw_text (int x, int y, int w, int h, unsigned code,
		uint32_t fg, uint32_t bg, uint32_t flags)
{
  struct ohos_draw_command cmd = { .op = OHOS_OP_TEXT,
			       .x = x, .y = y, .w = w, .h = h,
			       .code = code, .fg = fg, .bg = bg,
			       .flags = flags };
  enqueue (&cmd);
}

void
ohos_draw_image (int x, int y, int w, int h, const void *rgba, unsigned len)
{
  if (w <= 0 || h <= 0 || len == 0)
    {
      char dbg[128];
      snprintf (dbg, sizeof dbg, "image draw skipped x=%d y=%d w=%d h=%d len=%u",
		x, y, w, h, len);
      ohos_log (dbg);
      return;
    }
  {
    static int image_logs;
    if (image_logs++ < 8)
      {
	char dbg[128];
	snprintf (dbg, sizeof dbg, "image draw x=%d y=%d w=%d h=%d len=%u",
		  x, y, w, h, len);
	ohos_log (dbg);
      }
  }
  /* The command must own its pixels: it is consumed later, on the UI
     thread, while the caller's buffer is already gone.  */
  void *owned = xmalloc (len);

  memcpy (owned, rgba, len);
  struct ohos_draw_command cmd = { .op = OHOS_OP_IMAGE,
			       .x = x, .y = y, .w = w, .h = h,
			       .ptr = owned, .ptr_len = len };
  enqueue (&cmd);
}

void
ohos_draw_cursor (int x, int y, int w, int h, int bar, uint32_t fg,
		  uint32_t bg, uint32_t flags, int kind)
{
  struct ohos_draw_command cmd = { .op = OHOS_OP_CURSOR,
			       .x = x, .y = y, .w = w, .h = h,
			       .code = (unsigned) bar,
			       .fg = fg, .bg = bg,
			       .flags = flags | ((uint32_t) kind << 1) };
  enqueue (&cmd);
}

void
ohos_draw_scroll (int x, int from_y, int width, int height, int to_y)
{
  struct ohos_draw_command cmd = { .op = OHOS_OP_SCROLL,
			       .x = x, .y = from_y, .w = width,
			       .h = height, .to_y = to_y };
  enqueue (&cmd);
}

void
ohos_draw_present (void)
{
  if (probe_present_pending)
    {
      struct timespec ts;
      clock_gettime (CLOCK_MONOTONIC, &ts);
      uint64_t now = (uint64_t) ts.tv_sec * 1000000000ull + ts.tv_nsec;
      if (probe_logs++ < 100000)
        {
          char dbg[128];
          snprintf (dbg, sizeof dbg, "pop->present seq=%llu %.1f us",
                    (unsigned long long) probe_pop_seq,
                    (double) (now - probe_pop_ns) / 1000.0);
          ohos_log (dbg);
        }
      probe_present_pending = false;
    }
  struct ohos_draw_command cmd = { .op = OHOS_OP_PRESENT };
  enqueue (&cmd);
  if (!ohos_direct_mode () && host_api && host_api->draw.wakeup)
    host_api->draw.wakeup ();
}

void
ohos_draw_begin (unsigned epoch)
{
  struct ohos_draw_command cmd = { .op = OHOS_OP_BEGIN, .epoch = epoch };
  draw_epoch = epoch;
  enqueue (&cmd);
}

void
ohos_draw_end (unsigned epoch)
{
  struct ohos_draw_command cmd = { .op = OHOS_OP_END, .epoch = epoch };
  enqueue (&cmd);
}

void
ohos_pump_draw_commands (void)
{
}


/* ------------------------------------------------------------------ */
/* Input queue: ArkUI UI thread -> Emacs read_socket_hook.            */

#define OHOS_INPUT_CAP 256
#define OHOS_INPUT_MASK (OHOS_INPUT_CAP - 1)

struct ohos_input_cmd
{
  unsigned key;
  unsigned modifiers;
  int mouse;
  /* A pointer move with no button transition: feeds note_mouse_highlight
     (mouse-face, hand pointer) and `frame->mouse_moved'.  */
  int motion;
  int button, x, y, press;
  int wheel;
  uint64_t enqueue_ns;
  uint64_t seq;
};

static struct ohos_input_cmd input_ring[OHOS_INPUT_CAP];
static unsigned input_head, input_tail, input_count;
static unsigned long input_dropped;
static uint64_t input_seq;
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
static int input_pipe[2] = { -1, -1 };


void
ohos_input_raw (const char *data, size_t len)
{
  for (size_t i = 0; i < len; i++)
    ohos_input_key (((unsigned char) data[i]) | OHOS_INPUT_RAW_FLAG, 0, 1);
}

void
ohos_create_input_pipe (void)
{
  if (input_pipe[0] >= 0)
    return;
  if (pipe (input_pipe) != 0)
    {
      input_pipe[0] = input_pipe[1] = -1;
      return;
    }
  fcntl (input_pipe[0], F_SETFL, O_NONBLOCK);
  fcntl (input_pipe[1], F_SETFL, O_NONBLOCK);
  fcntl (input_pipe[0], F_SETFD, FD_CLOEXEC);
  fcntl (input_pipe[1], F_SETFD, FD_CLOEXEC);
}

/* Wake the command loop only when the queue was empty: one byte per
   batch is enough, and a byte per event keeps Emacs from ever going
   idle while the pointer moves.  */
static void
ohos_signal_input (int was_empty)
{
  /* Always poke the pipe.  Signalling only when the queue was empty
     loses a wakeup when a key arrives while Emacs is draining the
     queue but after the previous byte was consumed: the event stays
     queued until some later timer poll.  The pipe is non-blocking and
     Emacs drains it in read_socket_hook, so an extra byte is cheap.  */
  (void) was_empty;
  if (input_pipe[1] >= 0)
    {
      char byte = 1;
      (void) write (input_pipe[1], &byte, 1);
    }
}

int
ohos_input_wait_fd (void)
{
  return input_pipe[0];
}

int
ohos_input_key (unsigned key, unsigned modifiers, int pressed)
{
  if (!pressed)
    return 0;
  int was_empty;

  struct timespec kts;
  clock_gettime (CLOCK_MONOTONIC, &kts);
  uint64_t enqueue_ns
      = (uint64_t) kts.tv_sec * 1000000000ull + kts.tv_nsec;
  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].enqueue_ns = enqueue_ns;
      input_ring[input_tail].seq = ++input_seq;
      input_ring[input_tail].key = key;
      input_ring[input_tail].modifiers = modifiers;
      input_ring[input_tail].mouse = 0;
      input_ring[input_tail].motion = 0;
      input_ring[input_tail].wheel = 0;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  ohos_signal_input (was_empty);
  return 0;
}

void
ohos_input_mouse (int button, int x, int y, unsigned modifiers, int press)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].mouse = 1;
      input_ring[input_tail].motion = 0;
      input_ring[input_tail].wheel = 0;
      input_ring[input_tail].button = button;
      input_ring[input_tail].x = x;
      input_ring[input_tail].y = y;
      input_ring[input_tail].modifiers = modifiers;
      input_ring[input_tail].press = press;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  ohos_signal_input (was_empty);
}

/* Pointer motion without a button change.  Mirrors android_input_motion:
   without this the backend never calls note_mouse_highlight, so there is
   no `mouse-face` (link / mode line highlight), no hand pointer, and
   Emacs' `frame_make_pointer_visible' can never run (task book B-11).  */
void
ohos_input_motion (int x, int y, unsigned modifiers)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].mouse = 1;
      input_ring[input_tail].motion = 1;
      input_ring[input_tail].wheel = 0;
      input_ring[input_tail].button = -1;
      input_ring[input_tail].x = x;
      input_ring[input_tail].y = y;
      input_ring[input_tail].modifiers = modifiers;
      input_ring[input_tail].press = 0;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  ohos_signal_input (was_empty);
}

void
ohos_input_wheel (int delta, int col, int row, unsigned modifiers)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].key = 0;
      input_ring[input_tail].modifiers = 0;
      input_ring[input_tail].mouse = 0;
      input_ring[input_tail].motion = 0;
      input_ring[input_tail].wheel = 1;
      input_ring[input_tail].modifiers = modifiers;
      input_ring[input_tail].button = delta > 0 ? 1 : 0;
      input_ring[input_tail].x = col;
      input_ring[input_tail].y = row;
      input_ring[input_tail].press = 0;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  ohos_signal_input (was_empty);
}

int
ohos_pop_input (unsigned *key, unsigned *modifiers, int *mouse,
		int *button, int *x, int *y, int *press, int *motion,
		int *wheel)
{
  int found = 0;
  pthread_mutex_lock (&input_lock);
  if (input_count != 0)
    {
      *key = input_ring[input_head].key;
      *modifiers = input_ring[input_head].modifiers;
      *mouse = input_ring[input_head].mouse;
      if (motion)
	*motion = input_ring[input_head].motion;
      if (wheel)
	*wheel = input_ring[input_head].wheel;
      *button = input_ring[input_head].button;
      *x = input_ring[input_head].x;
      *y = input_ring[input_head].y;
      *press = input_ring[input_head].press;
      uint64_t enqueue_ns = input_ring[input_head].enqueue_ns;
      uint64_t seq = input_ring[input_head].seq;
      input_head = (input_head + 1) & OHOS_INPUT_MASK;
      input_count--;
      found = 1;
      if (enqueue_ns)
        {
          struct timespec pts;
          clock_gettime (CLOCK_MONOTONIC, &pts);
          probe_pop_ns
              = (uint64_t) pts.tv_sec * 1000000000ull + pts.tv_nsec;
          probe_pop_seq = seq;
          probe_after_pop = true;
          probe_present_pending = true;
          if (probe_logs < 100000)
            {
              char dbg[128];
              snprintf (dbg, sizeof dbg, "input queue seq=%llu %.1f us",
                        (unsigned long long) seq,
                        (double) (probe_pop_ns - enqueue_ns) / 1000.0);
              ohos_log (dbg);
            }
        }
    }
  pthread_mutex_unlock (&input_lock);
  return found;
}

void
ohos_input_text (const char *text)
{
  const unsigned char *p = (const unsigned char *) text;
  if (!p)
    return;
  while (*p)
    {
      unsigned cp = 0;
      int n = 1;
      if (*p < 0x80)
	cp = *p;
      else if ((*p & 0xe0) == 0xc0 && p[1])
	{
	  cp = ((*p & 0x1f) << 6) | (p[1] & 0x3f);
	  n = 2;
	}
      else if ((*p & 0xf0) == 0xe0 && p[1] && p[2])
	{
	  cp = ((*p & 0x0f) << 12) | ((p[1] & 0x3f) << 6)
	    | (p[2] & 0x3f);
	  n = 3;
	}
      else if ((*p & 0xf8) == 0xf0 && p[1] && p[2] && p[3])
	{
	  cp = ((*p & 0x07) << 18) | ((p[1] & 0x3f) << 12)
	    | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f);
	  n = 4;
	}
      else
	{
	  p++;
	  continue;
	}
      struct timespec tts;
      clock_gettime (CLOCK_MONOTONIC, &tts);
      pthread_mutex_lock (&input_lock);
      if (input_count == OHOS_INPUT_CAP)
	input_dropped++;
      else
	{
	  input_ring[input_tail].enqueue_ns
	      = (uint64_t) tts.tv_sec * 1000000000ull + tts.tv_nsec;
	  input_ring[input_tail].seq = ++input_seq;
	  input_ring[input_tail].key = cp | OHOS_INPUT_TEXT_FLAG;
	  input_ring[input_tail].modifiers = 0;
	  input_ring[input_tail].mouse = 0;
	  input_ring[input_tail].motion = 0;
	  input_ring[input_tail].wheel = 0;
	  input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
	  input_count++;
	}
      pthread_mutex_unlock (&input_lock);
      p += n;
    }
  if (input_pipe[1] >= 0)
    {
      char byte = 1;
      (void) write (input_pipe[1], &byte, 1);
    }
}

void
ohos_input_expose (int x, int y, int width, int height)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].key = OHOS_INPUT_EXPOSE_FLAG;
      input_ring[input_tail].modifiers = 0;
      input_ring[input_tail].mouse = 0;
      input_ring[input_tail].wheel = 0;
      input_ring[input_tail].button = width;
      input_ring[input_tail].x = x;
      input_ring[input_tail].y = y;
      input_ring[input_tail].press = height;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  ohos_signal_input (was_empty);
}

static int ohos_last_surface_width;
static int ohos_last_surface_height;
static pthread_mutex_t surface_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t surface_ready = PTHREAD_COND_INITIALIZER;

/* The host reports the surface size from the UI thread, usually while Emacs
   is still starting and before any frame exists.  Remember it so frame
   creation can adopt the real geometry instead of inventing one.  */
void
ohos_last_surface_size (int *width, int *height)
{
  pthread_mutex_lock (&surface_lock);
  *width = ohos_last_surface_width;
  *height = ohos_last_surface_height;
  pthread_mutex_unlock (&surface_lock);
}

/* Wait until the host has reported a surface size, like the X11 backend
   waiting for the window manager's ConfigureNotify during frame creation.
   Returns false when the timeout expires.  */
bool
ohos_wait_surface_size (int timeout_ms)
{
  struct timespec deadline;
  bool ready = false;

  clock_gettime (CLOCK_REALTIME, &deadline);
  deadline.tv_sec += timeout_ms / 1000;
  deadline.tv_nsec += (long) (timeout_ms % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L)
    {
      deadline.tv_sec += 1;
      deadline.tv_nsec -= 1000000000L;
    }

  pthread_mutex_lock (&surface_lock);
  while (ohos_last_surface_width <= 0)
    {
      if (pthread_cond_timedwait (&surface_ready, &surface_lock, &deadline)
	  == ETIMEDOUT)
	break;
    }
  ready = ohos_last_surface_width > 0;
  pthread_mutex_unlock (&surface_lock);
  return ready;
}

void
ohos_input_resize (int width, int height)
{
  if (width <= 0 || height <= 0)
    return;
  int was_empty;

  pthread_mutex_lock (&surface_lock);
  ohos_last_surface_width = width;
  ohos_last_surface_height = height;
  pthread_cond_broadcast (&surface_ready);
  pthread_mutex_unlock (&surface_lock);

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].key = OHOS_INPUT_RESIZE_FLAG
	| ((unsigned) width & 0xffffu);
      input_ring[input_tail].modifiers = (unsigned) height;
      input_ring[input_tail].mouse = 0;
      input_ring[input_tail].wheel = 0;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  ohos_signal_input (was_empty);
}

void
ohos_input_focus (int focused)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].key = OHOS_INPUT_FOCUS_FLAG;
      input_ring[input_tail].modifiers = focused ? 1 : 0;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  ohos_signal_input (was_empty);
}

/* ------------------------------------------------------------------ */
/* Host (ArkUI) chrome channel: Emacs -> UI thread.                   */

#define OHOS_HOST_QUEUE_CAP 32

static char host_cmds[OHOS_HOST_QUEUE_CAP][OHOS_HOST_CMD_MAX];
static unsigned host_head, host_tail, host_count;
static unsigned long host_dropped;
static pthread_mutex_t host_lock = PTHREAD_MUTEX_INITIALIZER;

/* One-slot answer channel: the UI thread stores the result of the
   pending request and wakes the Emacs thread through `result_pipe'.  */

static int result_pipe[2] = { -1, -1 };
static int result_id = -1;
static int result_index = -1;
static char result_text[OHOS_HOST_RESULT_TEXT];
static pthread_mutex_t result_lock = PTHREAD_MUTEX_INITIALIZER;

static void
ohos_host_queue_init (void)
{
  static bool initialized;
  if (initialized)
    return;
  initialized = true;
  if (pipe (result_pipe) == 0)
    {
      fcntl (result_pipe[0], F_SETFL, O_NONBLOCK);
      fcntl (result_pipe[1], F_SETFL, O_NONBLOCK);
      fcntl (result_pipe[0], F_SETFD, FD_CLOEXEC);
      fcntl (result_pipe[1], F_SETFD, FD_CLOEXEC);
    }
  else
    result_pipe[0] = result_pipe[1] = -1;
}

void
ohos_host_command (const char *fmt, ...)
{
  va_list ap;
  char buf[OHOS_HOST_CMD_MAX];

  ohos_host_queue_init ();
  va_start (ap, fmt);
  vsnprintf (buf, sizeof buf, fmt, ap);
  va_end (ap);

  pthread_mutex_lock (&host_lock);
  if (host_count == OHOS_HOST_QUEUE_CAP)
    host_dropped++;
  else
    {
      strncpy (host_cmds[host_tail], buf, OHOS_HOST_CMD_MAX - 1);
      host_cmds[host_tail][OHOS_HOST_CMD_MAX - 1] = '\0';
      host_tail = (host_tail + 1) % OHOS_HOST_QUEUE_CAP;
      host_count++;
    }
  pthread_mutex_unlock (&host_lock);
  if (host_api && host_api->frame.host_wakeup)
    host_api->frame.host_wakeup ();
}

int
ohos_host_take_command (char *buf, int cap)
{
  int found = 0;

  if (!buf || cap <= 0)
    return 0;
  ohos_host_queue_init ();
  pthread_mutex_lock (&host_lock);
  if (host_count != 0)
    {
      strncpy (buf, host_cmds[host_head], cap - 1);
      buf[cap - 1] = '\0';
      host_head = (host_head + 1) % OHOS_HOST_QUEUE_CAP;
      host_count--;
      found = 1;
    }
  pthread_mutex_unlock (&host_lock);
  return found;
}

void
ohos_host_result (int id, int index, const char *text)
{
  ohos_host_queue_init ();
  pthread_mutex_lock (&result_lock);
  result_id = id;
  result_index = index;
  if (text)
    {
      strncpy (result_text, text, OHOS_HOST_RESULT_TEXT - 1);
      result_text[OHOS_HOST_RESULT_TEXT - 1] = '\0';
    }
  else
    result_text[0] = '\0';
  pthread_mutex_unlock (&result_lock);
  if (result_pipe[1] >= 0)
    {
      char byte = 1;
      (void) write (result_pipe[1], &byte, 1);
    }
}

int
ohos_host_wait_result (int id, int timeout_ms, int *index,
		       char *text, int cap)
{
  /* A negative timeout waits forever: menus stay open until the host
     answers or sends menu-close.  */
  bool infinite = timeout_ms < 0;
  int remaining = infinite ? 20 : timeout_ms;

  ohos_host_queue_init ();
  for (;;)
    {
      int matched = 0;
      int got_index = -1;
      char got_text[OHOS_HOST_RESULT_TEXT];

      pthread_mutex_lock (&result_lock);
      if (result_id == id)
	{
	  matched = 1;
	  got_index = result_index;
	  strncpy (got_text, result_text, sizeof got_text - 1);
	  got_text[sizeof got_text - 1] = '\0';
	  result_id = -1;
	  result_index = -1;
	  result_text[0] = '\0';
	}
      pthread_mutex_unlock (&result_lock);
      if (matched)
	{
	  if (index)
	    *index = got_index;
	  if (text && cap > 0)
	    {
	      strncpy (text, got_text, cap - 1);
	      text[cap - 1] = '\0';
	    }
	  return 1;
	}

      if (result_pipe[0] < 0)
	{
	  struct timespec ts = { 0, 5 * 1000 * 1000 };
	  if (!infinite && remaining <= 0)
	    return 0;
	  nanosleep (&ts, NULL);
	  if (!infinite)
	    remaining -= 5;
	  continue;
	}

      int wait = infinite ? 20 : (remaining < 20 ? remaining : 20);
      if (wait < 0)
	wait = 0;
      struct pollfd pfd;
      pfd.fd = result_pipe[0];
      pfd.events = POLLIN;
      pfd.revents = 0;
      int rc = poll (&pfd, 1, wait);
      if (rc > 0 && (pfd.revents & POLLIN))
	{
	  char buf[32];
	  while (read (result_pipe[0], buf, sizeof buf) > 0)
	    ;
	}
      if (!infinite && remaining <= 20 && rc == 0)
	return 0;
      if (!infinite)
	remaining -= wait;
    }
}

void
ohos_host_menu_result (int id, int index)
{
  ohos_host_result (id, index, NULL);
}

int
ohos_menu_wait_result (int id, int timeout_ms)
{
  int index = -1;
  if (!ohos_host_wait_result (id, timeout_ms, &index, NULL, 0))
    return -2;
  return index;
}

void
ohos_host_drain (void)
{
  char buf[OHOS_HOST_CMD_MAX];
  while (ohos_host_take_command (buf, sizeof buf))
    ;
}
