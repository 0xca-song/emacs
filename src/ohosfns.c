/* OHOS frame backend, mirroring the Android androidfns.c layout.  */

#include <config.h>
#include <stdio.h>
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
#include "fontset.h"
#include "termhooks.h"
#include "keyboard.h"
#include "ohosgui.h"

extern Lisp_Object Fset_fontset_font (Lisp_Object, Lisp_Object,
				      Lisp_Object, Lisp_Object, Lisp_Object);

static struct terminal *
ohos_find_terminal (void)
{
  for (struct terminal *t = terminal_list; t; t = t->next_terminal)
    if (t->type == output_ohos)
      return t;
  return NULL;
}

static void
ohos_unwind_create_frame (Lisp_Object frame)
{
  /* Keep FRAME rooted while the C code below calls Lisp (fontset and
     face realization can allocate and run the GC).  */
  (void) frame;
}

struct frame *
ohos_create_frame (struct terminal *terminal)
{
  ohos_log ("ohos_create_frame enter");
  if (!terminal)
    terminal = ohos_find_terminal ();
  if (!terminal)
    error ("OHOS terminal is not initialized");

  specpdl_ref count = SPECPDL_INDEX ();
  struct frame *f = make_frame (true);
  ohos_log ("ohos_create_frame make_frame done");
  Lisp_Object frame;
  XSETFRAME (frame, f);
  record_unwind_protect (ohos_unwind_create_frame, frame);

  f->terminal = terminal;
  f->output_method = output_ohos;
  f->output_data.ohos = xzalloc (sizeof *f->output_data.ohos);
  f->output_data.ohos->display_info = terminal->display_info.ohos;
  f->output_data.ohos->width = 800;
  f->output_data.ohos->height = 600;
  f->output_data.ohos->fontset = fs_query_fontset (build_string ("fontset-default"), 0);
  f->output_data.ohos->parent_desc = 0;
  f->output_data.ohos->root_window = 0;

  FRAME_DISPLAY_INFO (f)->resx = 96;
  FRAME_DISPLAY_INFO (f)->resy = 96;
  f->column_width = 9;
  f->line_height = 18;
  ohosfont_register_frame (f);
  Lisp_Object font_obj = ohosfont_make_default_object (f);
  f->output_data.ohos->font = NILP (font_obj) ? NULL : XFONT_OBJECT (font_obj);
  if (!NILP (font_obj))
    {
      store_frame_param (f, Qfont, font_obj);
      f->output_data.ohos->fontset = fontset_from_font (font_obj);
      {
	/* Map the CJK charsets explicitly; fontset_from_font only maps
	   the registry's primary charset (unicode-bmp), leaving the
	   generic chinese-gb2312 fallback in front for CJK text.  */
	static const char *const cjk_charsets[] = {
	  "chinese-gb2312", "chinese-gbk", "chinese-big5-1",
	  "chinese-big5-2", "japanese-jisx0208", "korean-ksc5601",
	  "unicode-bmp", NULL
	};
	Lisp_Object fset_name = fontset_name (f->output_data.ohos->fontset);
	Lisp_Object spec = AREF (font_obj, FONT_NAME_INDEX);
	for (int i = 0; cjk_charsets[i]; i++)
	  Fset_fontset_font (fset_name, intern (cjk_charsets[i]),
			     spec, Qnil, Qnil);
      }
    }
  else
    f->output_data.ohos->fontset
      = fs_query_fontset (build_string ("fontset-default"), 0);
  ohos_log (NILP (font_obj) ? "ohos frame font open failed"
	    : "ohos frame font open ok");
  ohos_log ("ohos_create_frame font/fontset done");

  FRAME_FOREGROUND_PIXEL (f) = 0x000000;
  FRAME_BACKGROUND_PIXEL (f) = 0xffffff;
  f->vertical_scroll_bar_type = vertical_scroll_bar_none;
  f->horizontal_scroll_bars = false;
  f->can_set_window_size = true;
  store_frame_param (f, Qforeground_color, build_string ("black"));
  store_frame_param (f, Qbackground_color, build_string ("white"));

  init_frame_faces (f);
  ohos_log ("ohos_create_frame faces done");
  adjust_frame_size (f, 80 * 9, 24 * 18, 5, true, Qnil);
  ohos_log ("ohos_create_frame size done");

  f->terminal->reference_count++;
  Vframe_list = Fcons (frame, Vframe_list);
  SET_FRAME_VISIBLE (f, true);
  Fselect_frame (frame, Qnil);
  SET_FRAME_GARBAGED (f);
  ohos_log ("ohos_create_frame selected");
  {
    /* Drop the bootstrap tty frame.  Only the native OHOS frame may
       remain registered after startup.  */
    Lisp_Object tail, next;
    for (tail = Vframe_list; CONSP (tail); tail = next)
      {
	next = XCDR (tail);
	Lisp_Object frame_object = XCAR (tail);
	if (!FRAME_OHOS_P (XFRAME (frame_object)))
	  Fdelete_frame (frame_object, Qt);
      }
  }
  {
    int frame_count = 0;
    Lisp_Object tail;
    for (tail = Vframe_list; CONSP (tail); tail = XCDR (tail))
      frame_count++;
    struct frame *selected = SELECTED_FRAME ();
    char buf[160];
    snprintf (buf, sizeof buf,
	      "frame-list count=%d selected=%p selected_ohos=%d",
	      frame_count, (void *) selected,
	      selected ? FRAME_OHOS_P (selected) : 0);
    ohos_log (buf);
    int index = 0;
    for (tail = Vframe_list; CONSP (tail); tail = XCDR (tail))
      {
	struct frame *item = XFRAME (XCAR (tail));
	snprintf (buf, sizeof buf, "frame[%d] ptr=%p ohos=%d selected=%d",
		  index++, (void *) item, FRAME_OHOS_P (item),
		  item == selected);
	ohos_log (buf);
      }
  }
  unbind_to (count, Qnil);
  return f;
}

DEFUN ("ohos-create-frame", Fohos_create_frame, Sohos_create_frame, 0, 1, 0,
       doc: /* Create a native OHOS frame.  */)
  (Lisp_Object parms)
{
  (void) parms;
  struct frame *f = ohos_create_frame (NULL);
  Lisp_Object obj;
  XSETFRAME (obj, f);
  return obj;
}

DEFUN ("ohos-set-title", Fohos_set_title, Sohos_set_title, 1, 1, 0,
       doc: /* Set the native window title to TITLE.  */)
  (Lisp_Object title)
{
  CHECK_STRING (title);
  ohos_set_title (SSDATA (title));
  return Qnil;
}

DEFUN ("ohos-set-clipboard", Fohos_set_clipboard, Sohos_set_clipboard,
       1, 1, 0,
       doc: /* Set the system clipboard to base64-encoded B64.  */)
  (Lisp_Object b64)
{
  CHECK_STRING (b64);
  ohos_set_clipboard (SSDATA (b64));
  return Qnil;
}

void
syms_of_ohosfns (void)
{
  defsubr (&Sohos_create_frame);
  defsubr (&Sohos_set_title);
  defsubr (&Sohos_set_clipboard);
}
