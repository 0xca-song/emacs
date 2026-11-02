/* OHOS native GUI redisplay skeleton.
   This is the display-engine side of the native window backend: the
   redisplay_interface consumes Emacs glyph rows and forwards cells to
   the OHOS frame sink.  Frame/terminal creation is added in the next
   patch; this file deliberately has no tty/pty dependency.  */

#include <config.h>
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
  static int calls;
  if ((calls++ % 200) == 0)
    ohos_log ("ohos_draw_glyph_string");
  if (!s || !s->f || !s->first_glyph || s->nchars <= 0 || !s->face)
    return;

  int line_height = FRAME_LINE_HEIGHT (s->f);
  if (line_height <= 0)
    return;

  int pixel_x = s->x;
  for (int i = 0; i < s->nchars; i++)
    {
      struct glyph *g = s->first_glyph + i;
      if (g->type != CHAR_GLYPH)
	continue;

      int width = g->pixel_width;
      if (width <= 0)
	width = FRAME_COLUMN_WIDTH (s->f);
      if (width <= 0)
	continue;

      uint32_t flags = (s->face->background != s->face->foreground) ? 1 : 0;
      ohos_draw_text (pixel_x, s->y, width, line_height, g->u.ch,
		      (uint32_t) s->face->foreground,
		      (uint32_t) s->face->background, flags);
      pixel_x += width;
    }
}

static void
ohos_clear_frame_area (struct frame *f, int x, int y, int width, int height)
{
  ohos_draw_fill (x, y, width, height, (uint32_t) FRAME_BACKGROUND_PIXEL (f));
}

static void
ohos_clear_frame (struct frame *f)
{
  static int calls;
  if ((calls++ % 20) == 0)
    ohos_log ("ohos_clear_frame");
  ohos_draw_clear ((uint32_t) FRAME_BACKGROUND_PIXEL (f));
}

static void
ohos_flush (struct frame *f)
{
  static int calls;
  if ((calls++ % 20) == 0)
    {
      char buf[160];
      struct frame *sf = SELECTED_FRAME ();
      snprintf (buf, sizeof buf, "ohos_flush frame=%p selected=%p live=%d",
		(void *) f, (void *) sf, FRAME_LIVE_P (f));
      ohos_log (buf);
    }
  ohos_draw_present ();
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
  (void) row; (void) type; (void) active_p;
  if (!on_p || !w)
    return;
  struct frame *f = WINDOW_XFRAME (w);
  int line_height = FRAME_LINE_HEIGHT (f);
  if (line_height <= 0)
    return;
  if (width <= 0)
    width = FRAME_COLUMN_WIDTH (f);
  ohos_draw_cursor (x, y, width, line_height,
		    (uint32_t) FRAME_FOREGROUND_PIXEL (f),
		    (uint32_t) FRAME_BACKGROUND_PIXEL (f), 1);
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
  terminal->clear_frame_hook = ohos_clear_frame;
  terminal->defined_color_hook = ohos_defined_color;
  terminal->query_frame_background_color
    = ohos_query_frame_background_color;
  terminal->get_focus_frame = NULL;
  return terminal;
}

struct redisplay_interface *
ohos_get_redisplay_interface (void)
{
  return &ohos_redisplay_interface;
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
  syms_of_ohosfns ();
}
