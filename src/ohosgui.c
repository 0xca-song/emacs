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

struct ohos_display_info *
ohos_term_init (void)
{
  fprintf (stderr, "[ohos] term_init begin\n");
  struct ohos_display_info *dpyinfo = xzalloc (sizeof *dpyinfo);
  dpyinfo->resx = 96.0;
  dpyinfo->resy = 96.0;
  dpyinfo->n_planes = 24;
  dpyinfo->color_p = 1;
  dpyinfo->smallest_font_height = 18;
  dpyinfo->smallest_char_width = 9;
  dpyinfo->next = NULL;
  struct terminal *terminal = ohos_create_terminal (dpyinfo);
  if (current_kboard == initial_kboard)
    current_kboard = terminal->kboard;
  terminal->kboard->reference_count++;
  terminal->reference_count++;
  terminal->name = xstrdup ("ohos");
  dpyinfo->name_list_element = Fcons (build_string ("ohos"), Qnil);
  Fohos_create_frame (Qnil);
  fprintf (stderr, "[ohos] term_init end\n");
  return dpyinfo;
}

struct frame *
ohos_create_frame (struct terminal *terminal)
{
  struct frame *f = make_frame (true);
  f->terminal = terminal;
  f->output_method = output_ohos;
  f->output_data.ohos = xzalloc (sizeof *f->output_data.ohos);
  f->output_data.ohos->display_info = terminal->display_info.ohos;
  f->output_data.ohos->width = 800;
  f->output_data.ohos->height = 600;
  f->output_data.ohos->fontset = -1;
  f->column_width = 9;
  f->line_height = 18;
  f->text_cols = f->total_cols = 80;
  f->text_lines = f->total_lines = 24;
  fprintf (stderr, "[ohos] frame before font\n");
  f->output_data.ohos->font = ohosfont_make_default (f);
  fprintf (stderr, "[ohos] frame font=%p\n", (void *) f->output_data.ohos->font);
  f->output_data.ohos->parent_desc = 0;
  f->output_data.ohos->root_window = 0;
  fprintf (stderr, "[ohos] frame created\n");
  return f;
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

struct redisplay_interface *
ohos_get_redisplay_interface (void)
{
  return &ohos_redisplay_interface;
}

void
ohos_frame_text (int x, int y, int width, unsigned code, uint32_t fg,
                 uint32_t bg, int with_background)
{
  (void) x; (void) y; (void) width; (void) code;
  (void) fg; (void) bg; (void) with_background;
}

DEFUN ("ohos-create-frame", Fohos_create_frame, Sohos_create_frame, 0, 1, 0,
       doc: /* Create a native OHOS frame.  */)
  (Lisp_Object parms)
{
  (void) parms;
  fprintf (stderr, "[ohos] create_frame enter\n");
  struct frame *f = ohos_create_frame (terminal_list);
  Lisp_Object frame;
  XSETFRAME (frame, f);
  Vframe_list = Fcons (frame, Vframe_list);
  fprintf (stderr, "[ohos] create_frame done\n");
  return frame;
}

void
syms_of_ohosgui (void)
{
  DEFSYM (Qohos, "ohos");
}
