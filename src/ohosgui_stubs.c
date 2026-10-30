#include <config.h>
#include "lisp.h"
#include "frame.h"
#include "termhooks.h"
#include "dispextern.h"
#include "window.h"

Display_Info *
check_x_display_info (Lisp_Object frame)
{
  return NULL;
}

void
set_frame_menubar (struct frame *f, bool deep_p)
{
  (void) f; (void) deep_p;
}

void
frame_set_mouse_pixel_position (struct frame *f, int x, int y)
{
  (void) f; (void) x; (void) y;
}

Lisp_Object
Fx_hide_tip (Lisp_Object tip)
{
  (void) tip;
  return Qnil;
}

Lisp_Object
Fxw_display_color_p (Lisp_Object display)
{
  (void) display;
  return Qt;
}

Lisp_Object
Fx_display_grayscale_p (Lisp_Object display)
{
  (void) display;
  return Qnil;
}

char *
get_keysym_name (int keysym)
{
  (void) keysym;
  return (char *) "";
}

void
image_sync_to_pixmaps (struct frame *f, struct image *img)
{
  (void) f; (void) img;
}

void
image_pixmap_draw_cross (struct image *img, int x, int y)
{
  (void) img; (void) x; (void) y;
}
