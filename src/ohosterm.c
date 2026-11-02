/* OHOS terminal backend, mirroring androidterm.c.  */

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

bool
ohos_defined_color (struct frame *f, const char *color_name,
		    Emacs_Color *color_def, bool alloc, bool make_index)
{
  (void) f; (void) color_name; (void) color_def;
  (void) alloc; (void) make_index;
  /* The PoC face layer falls back to the frame foreground/background
     pixels when this hook returns false.  A real color parser is part
     of the MVP face work.  */
  return false;
}

void
ohos_query_frame_background_color (struct frame *f, Emacs_Color *color)
{
  color->pixel = FRAME_BACKGROUND_PIXEL (f);
  color->red = ((color->pixel >> 16) & 0xff) * 0x101;
  color->green = ((color->pixel >> 8) & 0xff) * 0x101;
  color->blue = (color->pixel & 0xff) * 0x101;
}

struct ohos_display_info *
ohos_term_init (void)
{
  ohos_log ("ohos_term_init enter");
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
  ohos_create_input_pipe ();
  int input_fd = ohos_input_wait_fd ();
  if (input_fd >= 0)
    add_keyboard_wait_descriptor (input_fd);
  terminal->kboard->reference_count++;
  terminal->reference_count++;
  terminal->name = xstrdup ("ohos");
  dpyinfo->name_list_element = Fcons (build_string ("ohos"), Qnil);
  KVAR (current_kboard, Vwindow_system) = Qohos;
  Fprovide (Qohos, Qnil);
  ohos_log ("ohos_term_init done");
  return dpyinfo;
}
