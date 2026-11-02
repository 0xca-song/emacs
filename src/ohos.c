/* OHOS native bridge: the NAPI-facing side of the backend.

   Mirrors the role of android.c: it is the only place that knows about
   the host callback ABI.  Emacs redisplay must not call the host
   directly; it enqueues drawing commands here, and the ArkUI thread
   drains them from ohos_pump_draw_commands.  */

#include <config.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "lisp.h"
#include "ohosgui.h"

enum ohos_draw_op
{
  OHOS_OP_CLEAR = 1,
  OHOS_OP_FILL,
  OHOS_OP_TEXT,
  OHOS_OP_CURSOR,
  OHOS_OP_SCROLL,
  OHOS_OP_PRESENT
};

struct ohos_draw_cmd
{
  int op;
  int x, y, w, h;
  int to_y;
  unsigned code;
  uint32_t fg, bg;
  uint32_t flags;
};

#define OHOS_DRAW_CMD_CAP 4096
#define OHOS_DRAW_CMD_MASK (OHOS_DRAW_CMD_CAP - 1)

static struct ohos_draw_cmd draw_ring[OHOS_DRAW_CMD_CAP];
static unsigned draw_head, draw_tail, draw_count;
static unsigned long draw_dropped;
static pthread_mutex_t draw_lock = PTHREAD_MUTEX_INITIALIZER;

static ohos_frame_sink_fn ohos_frame_sink;
static ohos_log_fn ohos_log_sink;
static ohos_clear_fn ohos_clear_sink;

static ohos_draw_clear_fn draw_clear_sink;
static ohos_draw_fill_fn draw_fill_sink;
static ohos_draw_text_fn draw_text_sink;
static ohos_draw_cursor_fn draw_cursor_sink;
static ohos_draw_scroll_fn draw_scroll_sink;
static ohos_draw_present_fn draw_present_sink;

void
ohos_set_log_sink (ohos_log_fn fn)
{
  ohos_log_sink = fn;
}

void
ohos_log (const char *s)
{
  if (ohos_log_sink)
    ohos_log_sink (s);
}

void
ohos_set_frame_sink (ohos_frame_sink_fn fn)
{
  ohos_frame_sink = fn;
}

void
ohos_frame_text (int x, int y, int width, unsigned code, uint32_t fg,
                 uint32_t bg, int with_background)
{
  if (ohos_frame_sink)
    ohos_frame_sink (x, y, width, code, fg, bg, with_background);
}

void
ohos_set_clear_sink (ohos_clear_fn fn)
{
  ohos_clear_sink = fn;
}

void
ohos_frame_clear (void)
{
  if (ohos_clear_sink)
    ohos_clear_sink ();
}

void
ohos_set_draw_clear_sink (ohos_draw_clear_fn fn)
{
  draw_clear_sink = fn;
}

void
ohos_set_draw_fill_sink (ohos_draw_fill_fn fn)
{
  draw_fill_sink = fn;
}

void
ohos_set_draw_text_sink (ohos_draw_text_fn fn)
{
  draw_text_sink = fn;
}

void
ohos_set_draw_cursor_sink (ohos_draw_cursor_fn fn)
{
  draw_cursor_sink = fn;
}

void
ohos_set_draw_scroll_sink (ohos_draw_scroll_fn fn)
{
  draw_scroll_sink = fn;
}

void
ohos_set_draw_present_sink (ohos_draw_present_fn fn)
{
  draw_present_sink = fn;
}

static void
enqueue (const struct ohos_draw_cmd *cmd)
{
  pthread_mutex_lock (&draw_lock);
  if (draw_count == OHOS_DRAW_CMD_CAP)
    draw_dropped++;
  else
    {
      draw_ring[draw_tail] = *cmd;
      draw_tail = (draw_tail + 1) & OHOS_DRAW_CMD_MASK;
      draw_count++;
    }
  pthread_mutex_unlock (&draw_lock);
}

void
ohos_draw_clear (uint32_t bg)
{
  struct ohos_draw_cmd cmd = { .op = OHOS_OP_CLEAR, .bg = bg };
  enqueue (&cmd);
}

void
ohos_draw_fill (int x, int y, int w, int h, uint32_t color)
{
  struct ohos_draw_cmd cmd = { .op = OHOS_OP_FILL,
			       .x = x, .y = y, .w = w, .h = h,
			       .bg = color };
  enqueue (&cmd);
}

void
ohos_draw_text (int x, int y, int w, int h, unsigned code,
		uint32_t fg, uint32_t bg, uint32_t flags)
{
  struct ohos_draw_cmd cmd = { .op = OHOS_OP_TEXT,
			       .x = x, .y = y, .w = w, .h = h,
			       .code = code, .fg = fg, .bg = bg,
			       .flags = flags };
  enqueue (&cmd);
}

void
ohos_draw_cursor (int x, int y, int w, int h, uint32_t fg, uint32_t bg,
		  uint32_t flags)
{
  struct ohos_draw_cmd cmd = { .op = OHOS_OP_CURSOR,
			       .x = x, .y = y, .w = w, .h = h,
			       .fg = fg, .bg = bg, .flags = flags };
  enqueue (&cmd);
}

void
ohos_draw_scroll (int x, int from_y, int width, int height, int to_y)
{
  struct ohos_draw_cmd cmd = { .op = OHOS_OP_SCROLL,
			       .x = x, .y = from_y, .w = width,
			       .h = height, .to_y = to_y };
  enqueue (&cmd);
}

void
ohos_draw_present (void)
{
  struct ohos_draw_cmd cmd = { .op = OHOS_OP_PRESENT };
  enqueue (&cmd);
}

void
ohos_pump_draw_commands (void)
{
  for (;;)
    {
      struct ohos_draw_cmd cmd;
      pthread_mutex_lock (&draw_lock);
      if (draw_count == 0)
	{
	  pthread_mutex_unlock (&draw_lock);
	  break;
	}
      cmd = draw_ring[draw_head];
      draw_head = (draw_head + 1) & OHOS_DRAW_CMD_MASK;
      draw_count--;
      pthread_mutex_unlock (&draw_lock);

      switch (cmd.op)
	{
	case OHOS_OP_CLEAR:
	  if (draw_clear_sink)
	    draw_clear_sink (cmd.bg);
	  break;
	case OHOS_OP_FILL:
	  if (draw_fill_sink)
	    draw_fill_sink (cmd.x, cmd.y, cmd.w, cmd.h, cmd.bg);
	  break;
	case OHOS_OP_TEXT:
	  if (draw_text_sink)
	    draw_text_sink (cmd.x, cmd.y, cmd.w, cmd.h, cmd.code,
			    cmd.fg, cmd.bg, cmd.flags);
	  break;
	case OHOS_OP_CURSOR:
	  if (draw_cursor_sink)
	    draw_cursor_sink (cmd.x, cmd.y, cmd.w, cmd.h,
			      cmd.fg, cmd.bg, cmd.flags);
	  break;
	case OHOS_OP_SCROLL:
	  if (draw_scroll_sink)
	    draw_scroll_sink (cmd.x, cmd.y, cmd.w, cmd.h, cmd.to_y);
	  break;
	case OHOS_OP_PRESENT:
	  if (draw_present_sink)
	    draw_present_sink ();
	  break;
	}
    }
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
  int button, x, y, press;
};

static struct ohos_input_cmd input_ring[OHOS_INPUT_CAP];
static unsigned input_head, input_tail, input_count;
static unsigned long input_dropped;
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
  pthread_mutex_lock (&input_lock);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].key = key;
      input_ring[input_tail].modifiers = modifiers;
      input_ring[input_tail].mouse = 0;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  if (input_pipe[1] >= 0)
    {
      char byte = 1;
      (void) write (input_pipe[1], &byte, 1);
    }
  return 0;
}

void
ohos_input_mouse (int button, int x, int y, unsigned modifiers, int press)
{
  pthread_mutex_lock (&input_lock);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].mouse = 1;
      input_ring[input_tail].button = button;
      input_ring[input_tail].x = x;
      input_ring[input_tail].y = y;
      input_ring[input_tail].modifiers = modifiers;
      input_ring[input_tail].press = press;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  if (input_pipe[1] >= 0)
    {
      char byte = 1;
      (void) write (input_pipe[1], &byte, 1);
    }
}

int
ohos_pop_input (unsigned *key, unsigned *modifiers, int *mouse,
		int *button, int *x, int *y, int *press)
{
  int found = 0;
  pthread_mutex_lock (&input_lock);
  if (input_count != 0)
    {
      *key = input_ring[input_head].key;
      *modifiers = input_ring[input_head].modifiers;
      *mouse = input_ring[input_head].mouse;
      *button = input_ring[input_head].button;
      *x = input_ring[input_head].x;
      *y = input_ring[input_head].y;
      *press = input_ring[input_head].press;
      input_head = (input_head + 1) & OHOS_INPUT_MASK;
      input_count--;
      found = 1;
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
      pthread_mutex_lock (&input_lock);
      if (input_count == OHOS_INPUT_CAP)
	input_dropped++;
      else
	{
	  input_ring[input_tail].key = cp | OHOS_INPUT_TEXT_FLAG;
	  input_ring[input_tail].modifiers = 0;
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
ohos_input_resize (int width, int height)
{
  if (width <= 0 || height <= 0)
    return;
  pthread_mutex_lock (&input_lock);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].key = OHOS_INPUT_RESIZE_FLAG
	| ((unsigned) width & 0xffffu);
      input_ring[input_tail].modifiers = (unsigned) height;
      input_ring[input_tail].mouse = 0;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  if (input_pipe[1] >= 0)
    {
      char byte = 1;
      (void) write (input_pipe[1], &byte, 1);
    }
}

void
ohos_input_focus (int focused)
{
  pthread_mutex_lock (&input_lock);
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
  if (input_pipe[1] >= 0)
    {
      char byte = 1;
      (void) write (input_pipe[1], &byte, 1);
    }
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
  int remaining = timeout_ms;

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
	  if (remaining <= 0)
	    return 0;
	  nanosleep (&ts, NULL);
	  remaining -= 5;
	  continue;
	}

      int wait = remaining < 20 ? remaining : 20;
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
      if (remaining <= 20 && rc == 0)
	return 0;
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
