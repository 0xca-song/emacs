/* OHOS stubs for the X11-shaped helpers used by the shared GUI code.

   Everything here replaced a NULL or empty stub: `check_x_display_info'
   in particular is called indirectly from Lisp (`x-get-resource',
   `x-display-color-p', ...) and must return the OHOS display info
   instead of NULL.  */

#include <config.h>
#include <string.h>
#include "lisp.h"
#include "frame.h"
#include "termhooks.h"
#include "dispextern.h"
#include "window.h"
#include "ohosgui.h"

/* Return the OHOS display info described by FRAME.
   FRAME nil means the selected frame, a frame object means that frame,
   and a string is accepted as a display name ("ohos" or
   "ohos-<id>").  */

Display_Info *
check_x_display_info (Lisp_Object frame)
{
  struct frame *f;

  if (NILP (frame))
    {
      f = SELECTED_FRAME ();
      if (!f || !FRAME_LIVE_P (f) || !FRAME_OHOS_P (f))
	error ("No OHOS display is available");
      return FRAME_DISPLAY_INFO (f);
    }

  if (FRAMEP (frame))
    {
      CHECK_LIVE_FRAME (frame);
      f = XFRAME (frame);
      if (!FRAME_OHOS_P (f))
	error ("Frame is not on an OHOS display");
      return FRAME_DISPLAY_INFO (f);
    }

  if (STRINGP (frame))
    {
      for (struct terminal *t = terminal_list; t; t = t->next_terminal)
	if (t->type == output_ohos
	    && (!strncmp (SSDATA (frame), t->name, SBYTES (frame))
		|| !strncmp (SSDATA (frame), "ohos", SBYTES (frame))))
	  return t->display_info.ohos;
      error ("Unknown OHOS display %s", SSDATA (frame));
    }

  wrong_type_argument (Qframe_live_p, frame);
}

/* Moving the system pointer is not part of the HarmonyOS application
   API; record the request so the host can implement it if a pointer
   warp API becomes available.  */

void
frame_set_mouse_pixel_position (struct frame *f, int x, int y)
{
  ohos_host_command ("pointer-warp %d %d", x, y);
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

/* Names for the keysyms the OHOS key bridge produces (see
   ohos_keysym_from_key in ohosgui.c).  `key-description' and the help
   echo area use these.  */

char *
get_keysym_name (int keysym)
{
  static char name[32];

  switch (keysym)
    {
    case 0xff08: return (char *) "BackSpace";
    case 0xff09: return (char *) "Tab";
    case 0xff0d: return (char *) "Return";
    case 0xff1b: return (char *) "Escape";
    case 0xff50: return (char *) "Home";
    case 0xff51: return (char *) "Left";
    case 0xff52: return (char *) "Up";
    case 0xff53: return (char *) "Right";
    case 0xff54: return (char *) "Down";
    case 0xff55: return (char *) "Prior";
    case 0xff56: return (char *) "Next";
    case 0xff57: return (char *) "End";
    case 0xffff: return (char *) "Delete";
    default:
      break;
    }

  if (keysym >= 0xffbe && keysym <= 0xffc9)
    {
      snprintf (name, sizeof name, "F%d", keysym - 0xffbe + 1);
      return name;
    }
  if (keysym > 0 && keysym < 0x80)
    {
      name[0] = (char) keysym;
      name[1] = '\0';
      return name;
    }
  snprintf (name, sizeof name, "0x%x", keysym);
  return name;
}

/* Images: the ArkUI drawing surface owns every bitmap, and the OHOS
   backend does not build X pixmaps.  */

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
