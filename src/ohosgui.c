/* OHOS native GUI redisplay skeleton.
   This is the display-engine side of the native window backend: the
   redisplay_interface consumes Emacs glyph rows and forwards cells to
   the OHOS frame sink.  Frame/terminal creation is added in the next
   patch; this file deliberately has no tty/pty dependency.  */

#include <config.h>
#include <execinfo.h>
#include <signal.h>
typedef void *Emacs_Pix_Container;
typedef void *Emacs_Cursor;
typedef void *Emacs_Pixmap;
typedef unsigned long RGB_PIXEL_COLOR;
typedef struct ohos_display_info Display_Info;
#include "lisp.h"
#include "frame.h"
#include "window.h"
#include "dispextern.h"
#include "font.h"
#include "termhooks.h"
#include "keyboard.h"
#include "ohosgui.h"

static void
ohos_draw_glyph_string (struct glyph_string *s)
{
  ohos_log ("draw_glyph_string");
  if (!s || !s->first_glyph || s->nchars <= 0)
    return;
  for (int i = 0; i < s->nchars; i++)
    {
      struct glyph *g = s->first_glyph + i;
      if (g->type != CHAR_GLYPH)
        continue;
      int w = ((g->u.ch >= 0x1100 && g->u.ch <= 0x115f)
               || (g->u.ch >= 0x2e80 && g->u.ch <= 0xa4cf)) ? 18 : 9;
      ohos_frame_text (s->x, s->y, w, g->u.ch,
                       (uint32_t) s->face->foreground,
                       (uint32_t) s->face->background,
                       s->face->background != s->face->foreground);
    }
}

static void
ohos_clear_frame_area (struct frame *f, int x, int y, int width, int height)
{
  (void) f; (void) x; (void) y; (void) width; (void) height;
}

static void
ohos_clear_frame (struct frame *f)
{
  (void) f;
}

static void
ohos_flush (struct frame *f)
{
  (void) f;
}

static void
ohos_after_update_window_line (struct window *w, struct glyph_row *row)
{
  (void) w; (void) row;
}

static void
ohos_scroll_run (struct window *w, struct run *run)
{
  (void) w; (void) run;
}

static void
ohos_draw_window_cursor (struct window *w, struct glyph_row *row,
                         int x, int y, enum text_cursor_kinds type,
                         int width, bool on_p, bool active_p)
{
  (void) w; (void) row; (void) x; (void) y; (void) type;
  (void) width; (void) on_p; (void) active_p;
}

static void
ohos_draw_vertical_window_border (struct window *w, int x, int y0, int y1)
{
  (void) w; (void) x; (void) y0; (void) y1;
}

static void
ohos_draw_window_divider (struct window *w, int x0, int x1, int y0, int y1)
{
  (void) w; (void) x0; (void) x1; (void) y0; (void) y1;
}

static void
ohos_clear_under_internal_border (struct frame *f)
{
  (void) f;
}

static void
ohos_define_frame_cursor (struct frame *f, Emacs_Cursor cursor)
{
  (void) f; (void) cursor;
}

static void
ohos_compute_glyph_string_overhangs (struct glyph_string *s)
{
  (void) s;
}

static void
ohos_define_fringe_bitmap (int which, unsigned short *bits, int h, int wd)
{
  (void) which; (void) bits; (void) h; (void) wd;
}

static void
ohos_destroy_fringe_bitmap (int which)
{
  (void) which;
}

static void
ohos_draw_fringe_bitmap (struct window *w, struct glyph_row *row,
                         struct draw_fringe_bitmap_params *p)
{
  (void) w; (void) row; (void) p;
}

int
XParseGeometry (const char *string, int *x, int *y, unsigned int *width,
                unsigned int *height)
{
  (void) string; (void) x; (void) y; (void) width; (void) height;
  return 0;
}
static struct redisplay_interface ohos_redisplay_interface;

static int
ohos_read_socket (struct terminal *terminal, struct input_event *hold_quit)
{
  (void) terminal; (void) hold_quit;
  return 0;
}

static void
ohos_delete_frame (struct frame *f)
{
  (void) f;
}

struct terminal *
ohos_create_terminal (struct ohos_display_info *dpyinfo)
{
  struct terminal *terminal = create_terminal (output_ohos,
                                                &ohos_redisplay_interface);
  terminal->display_info.ohos = dpyinfo;
  dpyinfo->terminal = terminal;
  terminal->kboard = allocate_kboard (Qohos);
  terminal->read_socket_hook = ohos_read_socket;
  terminal->delete_frame_hook = ohos_delete_frame;
  terminal->get_focus_frame = NULL;
  return terminal;
}

static void
ohos_crash_handler (int sig)
{
  void *frames[64];
  int n = backtrace (frames, 64);
  dprintf (2, "\n--- ohos crash signal %d ---\n", sig);
  backtrace_symbols_fd (frames, n, 2);
  signal (sig, SIG_DFL);
  raise (sig);
}

struct redisplay_interface *
ohos_get_redisplay_interface (void)
{
  return &ohos_redisplay_interface;
}

extern bool noninteractive;

DEFUN ("ohos-trace", Fohos_trace, Sohos_trace, 1, 1, 0,
       doc: /* Write a trace line to stderr.  */)
  (Lisp_Object msg)
{
  CHECK_STRING (msg);
  fprintf (stderr, "[ohos] %s\n", SDATA (msg));
  return Qnil;
}

static struct redisplay_interface ohos_redisplay_interface =
  {
    NULL, /* frame_parm_handlers */
    gui_produce_glyphs,
    gui_write_glyphs,
    gui_insert_glyphs,
    gui_clear_end_of_line,
    ohos_scroll_run,
    ohos_after_update_window_line,
    NULL,
    NULL,
    ohos_flush,
    gui_clear_window_mouse_face,
    gui_get_glyph_overhangs,
    gui_fix_overlapping_area,
    ohos_draw_fringe_bitmap,
    ohos_define_fringe_bitmap,
    ohos_destroy_fringe_bitmap,
    ohos_compute_glyph_string_overhangs,
    ohos_draw_glyph_string,
    ohos_define_frame_cursor,
    ohos_clear_frame_area,
    ohos_clear_under_internal_border,
    ohos_draw_window_cursor,
    ohos_draw_vertical_window_border,
    ohos_draw_window_divider,
    NULL,
    NULL,
    NULL,
    NULL,
  };


void
syms_of_ohosgui (void)
{
  DEFSYM (Qohos, "ohos");
  defsubr (&Sohos_trace);
  syms_of_ohosfns ();
}
