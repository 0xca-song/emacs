/* OHOS menu backend, mirroring androidmenu.c.  */

#include <config.h>
#include "lisp.h"
#include "frame.h"
#include "termhooks.h"
#include "dispextern.h"
#include "window.h"

void
set_frame_menubar (struct frame *f, bool deep_p)
{
  (void) f; (void) deep_p;
}

Lisp_Object
Fx_hide_tip (Lisp_Object tip)
{
  (void) tip;
  return Qnil;
}
