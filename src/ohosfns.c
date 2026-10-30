/* OHOS frame backend, mirroring the Android androidfns.c layout.  */

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

struct frame *
ohos_create_frame (struct terminal *terminal)
{
  struct frame *f = make_frame (true);
  Lisp_Object frame;
  XSETFRAME (frame, f);
  Vframe_list = Fcons (frame, Vframe_list);
  f->terminal = terminal;
  f->output_method = output_ohos;
  f->output_data.ohos = xzalloc (sizeof *f->output_data.ohos);
  f->output_data.ohos->display_info = terminal->display_info.ohos;
  f->output_data.ohos->width = 800;
  f->output_data.ohos->height = 600;
  f->output_data.ohos->fontset = -1;
  FRAME_DISPLAY_INFO (f)->resx = 96;
  FRAME_DISPLAY_INFO (f)->resy = 96;
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
  ohos_log (FRAME_WINDOW_P (f) ? "frame window_p=1" : "frame window_p=0");
  FRAME_FOREGROUND_PIXEL (f) = 0x000000;
  FRAME_BACKGROUND_PIXEL (f) = 0xffffff;
  f->vertical_scroll_bar_type = vertical_scroll_bar_none;
  f->horizontal_scroll_bars = false;
  f->can_set_window_size = true;
  store_frame_param (f, Qforeground_color, build_string ("black"));
  store_frame_param (f, Qbackground_color, build_string ("white"));
  init_frame_faces (f);
  adjust_frame_glyphs (f);
  Fselect_frame (frame, Qnil);
  ohos_log ("frame faces/glyphs initialized");
  return f;
}

DEFUN ("ohos-create-frame", Fohos_create_frame, Sohos_create_frame, 0, 1, 0,
       doc: /* Create a native OHOS frame.  */)
  (Lisp_Object parms)
{
  (void) parms;
  struct frame *f = ohos_create_frame (terminal_list);
  Lisp_Object obj;
  XSETFRAME (obj, f);
  return obj;
}

void
syms_of_ohosfns (void)
{
  defsubr (&Sohos_create_frame);
}
