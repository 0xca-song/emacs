/* OHOS native bridge: the NAPI-facing side of the backend.

   Mirrors the role of android.c: it is the only place that knows about
   the host callback ABI.  Surface events and input are immutable records
   produced by the ArkUI thread and consumed by the Emacs thread.  Pixel
   output never crosses this file: redisplay calls the statically linked
   per-frame renderer directly.  */

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
#include "ohos_render.h"

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
  snprintf (dbg, sizeof dbg, "OHOSDBG %s %.1f us", tag,
            (double) (now - probe_pop_ns) / 1000.0);
  ohos_log (dbg);
}

/* The host API installed by ohos_abi.c before Emacs starts.  NULL only
   during very early startup, before the host calls the run entry.  */
static const struct ohos_host_api *host_api;

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

/* ------------------------------------------------------------------ */
/* Frame registry: frame_id -> frame + renderer.  Emacs thread only.  */

#define OHOS_MAX_FRAMES 64

struct ohos_frame_slot
{
  uint32_t frame_id;
  struct frame *frame;
  struct ohos_renderer *renderer;
};

static struct ohos_frame_slot frame_slots[OHOS_MAX_FRAMES];
static void ohos_drop_surface_events (uint32_t frame_id);

uint32_t
ohos_frame_register (struct frame *f, struct ohos_renderer *renderer)
{
  if (!f || !renderer)
    return 0;
  for (int i = 0; i < OHOS_MAX_FRAMES; i++)
    if (frame_slots[i].frame_id == 0)
      {
        /* Prefer the lowest free id so a new frame can take over the
           boot surface (id 1) after the previous frame was deleted.  */
        uint32_t id = 1;
        while (id <= OHOS_MAX_FRAMES && ohos_frame_by_id (id))
          id++;
        if (id > OHOS_MAX_FRAMES)
          {
            ohos_log ("OHOSDBG frame id space exhausted");
            return 0;
          }
        frame_slots[i].frame_id = id;
        frame_slots[i].frame = f;
        frame_slots[i].renderer = renderer;
        {
          char dbg[96];
          snprintf (dbg, sizeof dbg, "OHOSDBG frame register id=%u",
                    frame_slots[i].frame_id);
          ohos_log (dbg);
        }
        return frame_slots[i].frame_id;
      }
  ohos_log ("OHOSDBG frame registry full");
  return 0;
}

void
ohos_frame_unregister (uint32_t frame_id)
{
  if (frame_id == 0)
    return;
  for (int i = 0; i < OHOS_MAX_FRAMES; i++)
    if (frame_slots[i].frame_id == frame_id)
      {
        frame_slots[i].frame_id = 0;
        frame_slots[i].frame = NULL;
        frame_slots[i].renderer = NULL;
        ohos_drop_surface_events (frame_id);
        {
          char dbg[96];
          snprintf (dbg, sizeof dbg, "OHOSDBG frame unregister id=%u",
                    frame_id);
          ohos_log (dbg);
        }
        return;
      }
}

struct frame *
ohos_frame_by_id (uint32_t frame_id)
{
  for (int i = 0; i < OHOS_MAX_FRAMES; i++)
    if (frame_slots[i].frame_id == frame_id)
      return frame_slots[i].frame;
  return NULL;
}

struct ohos_renderer *
ohos_renderer_by_frame_id (uint32_t frame_id)
{
  for (int i = 0; i < OHOS_MAX_FRAMES; i++)
    if (frame_slots[i].frame_id == frame_id)
      return frame_slots[i].renderer;
  return NULL;
}

/* ------------------------------------------------------------------ */
/* Surface lifecycle events from the ArkUI thread.  Immutable events are
   queued here; only the Emacs thread applies them to a frame context.  */

struct ohos_surface_cmd
{
  int kind; /* 0 created, 1 resized, 2 destroyed */
  uint32_t frame_id;
  void *window;
  int width, height;
  uint64_t generation;
};

#define OHOS_SURFACE_CAP 32
static struct ohos_surface_cmd surface_ring[OHOS_SURFACE_CAP];
static unsigned surface_head, surface_tail, surface_count;
static pthread_mutex_t surface_event_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t surface_ready = PTHREAD_COND_INITIALIZER;
static int boot_surface_width;
static int boot_surface_height;
static void ohos_signal_input (int was_empty);

static void
ohos_push_surface (int kind, uint32_t frame_id, void *window, int width,
                   int height, uint64_t generation, bool wake)
{
  if (frame_id == 1 && (kind == 0 || kind == 1) && width > 0 && height > 0)
    {
      pthread_mutex_lock (&surface_event_lock);
      boot_surface_width = width;
      boot_surface_height = height;
      pthread_cond_broadcast (&surface_ready);
      pthread_mutex_unlock (&surface_event_lock);
    }

  pthread_mutex_lock (&surface_event_lock);
  /* Coalesce a pending resize: the newest size wins.  */
  if (kind == 1 && surface_count > 0)
    {
      unsigned last
          = (surface_tail + OHOS_SURFACE_CAP - 1) & (OHOS_SURFACE_CAP - 1);
      if (surface_ring[last].kind == 1
          && surface_ring[last].frame_id == frame_id)
        {
          surface_ring[last].width = width;
          surface_ring[last].height = height;
          surface_ring[last].generation = generation;
          pthread_mutex_unlock (&surface_event_lock);
          return;
        }
    }
  if (surface_count == OHOS_SURFACE_CAP)
    {
      /* Keep the newest event; drop the oldest pending event.  */
      surface_head = (surface_head + 1) & (OHOS_SURFACE_CAP - 1);
      surface_count--;
    }
  struct ohos_surface_cmd *c = &surface_ring[surface_tail];
  c->kind = kind;
  c->frame_id = frame_id;
  c->window = window;
  c->width = width;
  c->height = height;
  c->generation = generation;
  surface_tail = (surface_tail + 1) & (OHOS_SURFACE_CAP - 1);
  surface_count++;
  pthread_mutex_unlock (&surface_event_lock);
  if (wake)
    ohos_signal_input (surface_count == 1);
}

void
ohos_surface_created (uint32_t frame_id, void *window, int width, int height,
                      uint64_t generation)
{
  ohos_log ("OHOSDBG surface enqueue created");
  ohos_push_surface (0, frame_id, window, width, height, generation, true);
}

void
ohos_surface_resized (uint32_t frame_id, int width, int height,
                      uint64_t generation)
{
  ohos_push_surface (1, frame_id, NULL, width, height, generation, true);
}

void
ohos_surface_destroyed (uint32_t frame_id, uint64_t generation)
{
  ohos_push_surface (2, frame_id, NULL, 0, 0, generation, true);
}

static void
ohos_drop_surface_events (uint32_t frame_id)
{
  pthread_mutex_lock (&surface_event_lock);
  for (unsigned i = 0; i < surface_count; i++)
    {
      unsigned index = (surface_head + i) & (OHOS_SURFACE_CAP - 1);
      if (surface_ring[index].frame_id == frame_id)
        surface_ring[index].kind = -1;
    }
  /* Compact the ring in place.  */
  unsigned write = surface_head;
  unsigned kept = 0;
  for (unsigned i = 0; i < surface_count; i++)
    {
      unsigned index = (surface_head + i) & (OHOS_SURFACE_CAP - 1);
      if (surface_ring[index].kind >= 0)
        {
          if (write != index)
            surface_ring[write] = surface_ring[index];
          write = (write + 1) & (OHOS_SURFACE_CAP - 1);
          kept++;
        }
    }
  surface_tail = write;
  surface_count = kept;
  pthread_mutex_unlock (&surface_event_lock);
}

void
ohos_apply_surface_events (void)
{
  struct ohos_surface_cmd pending[OHOS_SURFACE_CAP];
  unsigned n = 0;

  pthread_mutex_lock (&surface_event_lock);
  while (surface_count > 0)
    {
      pending[n++] = surface_ring[surface_head];
      surface_head = (surface_head + 1) & (OHOS_SURFACE_CAP - 1);
      surface_count--;
    }
  pthread_mutex_unlock (&surface_event_lock);

  for (unsigned i = 0; i < n; i++)
    {
      struct ohos_surface_cmd *c = &pending[i];
      struct ohos_renderer *renderer
          = ohos_renderer_by_frame_id (c->frame_id);
      if (!renderer)
        {
          /* A destroy event for an unknown frame is stale: the frame was
             already deleted, so there is nobody to notify.  Creation and
             resize can arrive before their frame and stay pending.  */
          if (c->kind == 2)
            continue;
          static int orphan_logs;
          if (orphan_logs++ < 8)
            {
              char dbg[128];
              snprintf (dbg, sizeof dbg,
                        "OHOSDBG surface orphan frame=%u kind=%d gen=%llu",
                        c->frame_id, c->kind,
                        (unsigned long long) c->generation);
              ohos_log (dbg);
            }
          /* It is already pending: do not wake the command loop again
             or an orphan event would spin read_socket forever.  */
          ohos_push_surface (c->kind, c->frame_id, c->window, c->width,
                             c->height, c->generation, false);
          continue;
        }
      struct OhosSurfaceEvent event = {
        .kind = (enum OhosSurfaceEventKind) c->kind,
        .frame_id = c->frame_id,
        .window = c->window,
        .surface_w = c->width,
        .surface_h = c->height,
        .generation = c->generation,
      };
      ohos_renderer_apply_surface_event (renderer, &event);
      /* Surface pixels drive the frame size.  Emacs computes the final
         content rect (menu bar, scroll bars, min/max size) and publishes
         it back through ohos_publish_frame_metrics.  */
      if (c->kind == 0 || c->kind == 1)
        {
          struct frame *f = ohos_frame_by_id (c->frame_id);
          if (f)
            ohos_handle_surface_resize (f, c->width, c->height);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Input queue: ArkUI UI thread -> Emacs read_socket_hook.            */

#define OHOS_INPUT_CAP 256
#define OHOS_INPUT_MASK (OHOS_INPUT_CAP - 1)

struct ohos_input_cmd
{
  uint32_t frame_id;
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
ohos_input_raw (uint32_t frame_id, const char *data, size_t len)
{
  for (size_t i = 0; i < len; i++)
    ohos_input_key (frame_id, ((unsigned char) data[i]) | OHOS_INPUT_RAW_FLAG,
                    0, 1);
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
ohos_input_key (uint32_t frame_id, unsigned key, unsigned modifiers,
                int pressed)
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
      input_ring[input_tail].frame_id = frame_id;
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
ohos_input_mouse (uint32_t frame_id, int button, int x, int y,
                  unsigned modifiers, int press)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].frame_id = frame_id;
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
   Emacs' `frame_make_pointer_visible' can never run.  */
void
ohos_input_motion (uint32_t frame_id, int x, int y, unsigned modifiers)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].frame_id = frame_id;
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
ohos_input_wheel (uint32_t frame_id, int delta, int col, int row,
                  unsigned modifiers)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].frame_id = frame_id;
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
ohos_pop_input (uint32_t *frame_id, unsigned *key, unsigned *modifiers,
                int *mouse, int *button, int *x, int *y, int *press,
                int *motion, int *wheel)
{
  int found = 0;
  pthread_mutex_lock (&input_lock);
  if (input_count != 0)
    {
      *frame_id = input_ring[input_head].frame_id;
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
              snprintf (dbg, sizeof dbg,
                        "OHOSDBG input-queue seq=%llu %.1f us",
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
ohos_input_menubar (uint32_t frame_id, int index)
{
  {
    char dbg[64];
    snprintf (dbg, sizeof dbg, "input menubar frame=%u index=%d",
              frame_id, index);
    ohos_log (dbg);
  }
  int was_empty;
  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].frame_id = frame_id;
      input_ring[input_tail].key = OHOS_INPUT_MENUBAR_FLAG | (unsigned) index;
      input_ring[input_tail].modifiers = 0;
      input_ring[input_tail].mouse = 0;
      input_ring[input_tail].motion = 0;
      input_ring[input_tail].wheel = 0;
      input_ring[input_tail].seq = ++input_seq;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  ohos_signal_input (was_empty);
}

void
ohos_input_text (uint32_t frame_id, const char *text)
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
          input_ring[input_tail].frame_id = frame_id;
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
ohos_input_expose (uint32_t frame_id, int x, int y, int width, int height)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].frame_id = frame_id;
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

void
ohos_last_surface_size (int *width, int *height)
{
  pthread_mutex_lock (&surface_event_lock);
  *width = boot_surface_width;
  *height = boot_surface_height;
  pthread_mutex_unlock (&surface_event_lock);
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

  pthread_mutex_lock (&surface_event_lock);
  while (boot_surface_width <= 0)
    {
      if (pthread_cond_timedwait (&surface_ready, &surface_event_lock,
                                  &deadline)
          == ETIMEDOUT)
        break;
    }
  ready = boot_surface_width > 0;
  pthread_mutex_unlock (&surface_event_lock);
  return ready;
}

void
ohos_input_resize (uint32_t frame_id, int width, int height)
{
  if (width <= 0 || height <= 0)
    return;
  int was_empty;

  if (frame_id == 1)
    {
      pthread_mutex_lock (&surface_event_lock);
      boot_surface_width = width;
      boot_surface_height = height;
      pthread_cond_broadcast (&surface_ready);
      pthread_mutex_unlock (&surface_event_lock);
    }

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].frame_id = frame_id;
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
ohos_input_focus (uint32_t frame_id, int focused)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].frame_id = frame_id;
      input_ring[input_tail].key = OHOS_INPUT_FOCUS_FLAG;
      input_ring[input_tail].modifiers = focused ? 1 : 0;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  ohos_signal_input (was_empty);
}

void
ohos_input_close_request (uint32_t frame_id)
{
  int was_empty;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].frame_id = frame_id;
      input_ring[input_tail].key = OHOS_INPUT_CLOSE_FLAG;
      input_ring[input_tail].modifiers = 0;
      input_ring[input_tail].mouse = 0;
      input_ring[input_tail].motion = 0;
      input_ring[input_tail].wheel = 0;
      input_tail = (input_tail + 1) & OHOS_INPUT_MASK;
      input_count++;
    }
  pthread_mutex_unlock (&input_lock);
  ohos_signal_input (was_empty);
}

void
ohos_input_window_create_failed (uint32_t frame_id, const char *reason)
{
  int was_empty;
  (void) reason;

  pthread_mutex_lock (&input_lock);
  was_empty = (input_count == 0);
  if (input_count == OHOS_INPUT_CAP)
    input_dropped++;
  else
    {
      input_ring[input_tail].frame_id = frame_id;
      input_ring[input_tail].key = OHOS_INPUT_WINDOW_FAILED_FLAG;
      input_ring[input_tail].modifiers = 0;
      input_ring[input_tail].mouse = 0;
      input_ring[input_tail].motion = 0;
      input_ring[input_tail].wheel = 0;
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
ohos_host_wait_result (int id, int timeout_ms, int *index, char *text,
                       int cap)
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
