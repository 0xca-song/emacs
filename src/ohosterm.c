/* OHOS terminal backend, mirroring androidterm.c.  */

#include <config.h>
#include <stdlib.h>
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

/* The display info of this terminal.  Its Lisp_Object members must be
   marked by the GC exactly like androidterm marks its colour map: without
   that the table (and the other Lisp values stored here) is collected and
   every colour lookup starts returning nil.  */

static struct ohos_display_info *ohos_display_list;

void
mark_ohosterm (void)
{
  if (!ohos_display_list)
    return;
  mark_object (ohos_display_list->color_map);
  mark_object (ohos_display_list->focus_frame);
  mark_object (ohos_display_list->name_list_element);
  mark_object (ohos_display_list->rdb);
  mark_object (ohos_display_list->default_name);
}

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
      Lisp_Object tem;
      /* `f' may be nil (or a non-OHOS frame) on the face realization
	 path; fall back to any OHOS display so the colour table is
	 always reachable.  */
      struct ohos_display_info *dpyinfo = NULL;

      if (f && FRAME_OHOS_P (f))
	dpyinfo = FRAME_DISPLAY_INFO (f);
      else
	for (struct terminal *t = terminal_list; t; t = t->next_terminal)
	  if (t->type == output_ohos && t->display_info.ohos)
	    {
	      dpyinfo = t->display_info.ohos;
	      break;
	    }

      if (dpyinfo && CONSP (dpyinfo->color_map))
	for (tem = dpyinfo->color_map; CONSP (tem); tem = XCDR (tem))
	  {
	    Lisp_Object entry = XCAR (tem);

	    if (CONSP (entry) && STRINGP (XCAR (entry))
		&& FIXNUMP (XCDR (entry))
		&& !strcasecmp (SSDATA (XCAR (entry)), color_name))
	      {
		rgb = (uint32_t) XFIXNUM (XCDR (entry));
		color_def->pixel = rgb;
		color_def->red = ((rgb >> 16) & 0xff) * 0x101;
		color_def->green = ((rgb >> 8) & 0xff) * 0x101;
		color_def->blue = (rgb & 0xff) * 0x101;
		return true;
	      }
	  }
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
  ohos_display_list = dpyinfo;
  dpyinfo->resx = 96.0;
  dpyinfo->resy = 96.0;
  dpyinfo->n_planes = 24;
  dpyinfo->color_p = 1;

  dpyinfo->smallest_font_height = 18;
  dpyinfo->smallest_char_width = 9;
  dpyinfo->next = NULL;
  /* xzalloc zeroes the structure, but every Lisp_Object field must be
     Qnil, not NULL: the GC and the frame code rely on it.  */
  dpyinfo->focus_frame = Qnil;
  dpyinfo->name_list_element = Qnil;
  dpyinfo->rdb = Qnil;
  dpyinfo->default_name = Qnil;
  dpyinfo->color_map = Qnil;
  reset_mouse_highlight (&dpyinfo->mouse_highlight);
  {
    char dbg[192];
    snprintf (dbg, sizeof dbg, "color: data-directory=%s",
	      STRINGP (Vdata_directory) ? SSDATA (Vdata_directory) : "(nil)");
    ohos_log (dbg);
  }
  dpyinfo->color_map
    = Fx_load_color_file (Fexpand_file_name (build_string ("rgb.txt"),
					     Vdata_directory));
  {
    char dbg[128];
    snprintf (dbg, sizeof dbg, "color: table entries=%d",
	      CONSP (dpyinfo->color_map) ? (int) Flength (dpyinfo->color_map) : 0);
    ohos_log (dbg);
  }
  struct terminal *terminal = ohos_create_terminal (dpyinfo);
  if (current_kboard == initial_kboard)
    current_kboard = terminal->kboard;
  ohos_create_input_pipe ();
  /* Give the frame that will be created its real geometry: the surface
     size is known to the host before any frame exists, and
     default-frame-alist is what Fmake_frame uses for width/height.  */
  if (ohos_wait_surface_size (3000))
    {
      int sw = 0, sh = 0;

      ohos_last_surface_size (&sw, &sh);
      if (sw > 0 && sh > 0)
	{
	  int cols = sw / 9, lines = sh / 18;

	  Vdefault_frame_alist
	    = Fcons (Fcons (Qwidth, make_fixnum (cols)),
		     Fcons (Fcons (Qheight, make_fixnum (lines)),
			    Vdefault_frame_alist));
	}
    }

  /* Report the focus state right away, like a window manager sending
     FocusIn when the window is mapped: the event is queued before any
     Lisp runs, so it is processed during startup and blink-cursor finds
     the frame focused when it first checks.  */
  ohos_input_focus (1);
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
