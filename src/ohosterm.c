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

struct ohos_display_info *
ohos_term_init (void)
{
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
  KVAR (current_kboard, Vwindow_system) = Qohos;
  Fprovide (Qohos, Qnil);
  return dpyinfo;
}
