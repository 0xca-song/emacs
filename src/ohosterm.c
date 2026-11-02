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
  static const struct { const char *name; uint32_t rgb; } colors[] = {
    { "black", 0x000000 }, { "white", 0xffffff },
    { "red", 0xff0000 }, { "green", 0x00ff00 }, { "blue", 0x0000ff },
    { "yellow", 0xffff00 }, { "magenta", 0xff00ff }, { "cyan", 0x00ffff },
    { "gray", 0x808080 }, { "grey", 0x808080 },
    { "darkgray", 0x404040 }, { "lightgray", 0xc0c0c0 },
    { "orange", 0xffa500 }, { "purple", 0x800080 },
  };
  uint32_t rgb = 0;
  (void) f; (void) alloc; (void) make_index;
  if (!color_name || !color_def)
    return false;
  if (color_name[0] == '#')
    {
      char *endp;
      unsigned long v = strtoul (color_name + 1, &endp, 16);
      if (*endp != '\0')
	return false;
      if (endp - color_name == 4)
	{
	  unsigned r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
	  rgb = (r * 0x11 << 16) | (g * 0x11 << 8) | (b * 0x11);
	}
      else if (endp - color_name == 7)
	rgb = (uint32_t) v;
      else
	return false;
    }
  else
    {
      size_t i;
      for (i = 0; i < sizeof colors / sizeof colors[0]; i++)
	if (!strcasecmp (color_name, colors[i].name))
	  {
	    rgb = colors[i].rgb;
	    break;
	  }
      if (i == sizeof colors / sizeof colors[0])
	return false;
    }
  color_def->pixel = rgb;
  color_def->red = ((rgb >> 16) & 0xff) * 0x101;
  color_def->green = ((rgb >> 8) & 0xff) * 0x101;
  color_def->blue = (rgb & 0xff) * 0x101;
  return true;
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
