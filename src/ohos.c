/* OHOS native bridge: the NAPI-facing side of the backend.

   Mirrors the role of android.c: it is the only place that knows about
   the host callback ABI.  Emacs redisplay must not call the host
   directly; it enqueues drawing commands here, and the ArkUI thread
   drains them from ohos_pump_draw_commands.  */

#include <config.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "lisp.h"
#include "ohosgui.h"

enum ohos_draw_op
{
  OHOS_OP_CLEAR = 1,
  OHOS_OP_FILL,
  OHOS_OP_TEXT,
  OHOS_OP_CURSOR,
  OHOS_OP_PRESENT
};

struct ohos_draw_cmd
{
  int op;
  int x, y, w, h;
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
};

static struct ohos_input_cmd input_ring[OHOS_INPUT_CAP];
static unsigned input_head, input_tail, input_count;
static unsigned long input_dropped;
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
static int input_pipe[2] = { -1, -1 };

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

int
ohos_pop_input (unsigned *key, unsigned *modifiers)
{
  int found = 0;
  pthread_mutex_lock (&input_lock);
  if (input_count != 0)
    {
      *key = input_ring[input_head].key;
      *modifiers = input_ring[input_head].modifiers;
      input_head = (input_head + 1) & OHOS_INPUT_MASK;
      input_count--;
      found = 1;
    }
  pthread_mutex_unlock (&input_lock);
  return found;
}
