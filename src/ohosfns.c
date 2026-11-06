/* OHOS frame backend, mirroring the Android androidfns.c layout.

   This file owns frame creation and the frame parameter handlers.  The
   redisplay side lives in ohosgui.c, the font driver in ohosfont.c.

   OHOS windows are owned by ArkUI, not by the application: Emacs cannot
   create or reparent a native window directly.  Window operations are
   therefore forwarded to the ArkUI page through the host command
   channel (`ohos_host_command'), which applies them to the
   window.Window object of the ability.  */

#include <config.h>
#include <stdio.h>
#include <string.h>
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
#include "coding.h"
#include "blockinput.h"
#include "ohosgui.h"

extern Lisp_Object Fset_fontset_font (Lisp_Object, Lisp_Object,
				      Lisp_Object, Lisp_Object, Lisp_Object);

void ohos_change_tab_bar_height (struct frame *, int);
void ohos_change_tool_bar_height (struct frame *, int);

static struct terminal *
ohos_find_terminal (void)
{
  for (struct terminal *t = terminal_list; t; t = t->next_terminal)
    if (t->type == output_ohos)
      return t;
  return NULL;
}

/* Decode COLOR_NAME for frame F.  DFLT is used for nil.  */
static unsigned long
ohos_decode_color (struct frame *f, Lisp_Object color_name,
		   unsigned long dflt)
{
  Emacs_Color color;

  color.pixel = dflt;
  color.red = color.green = color.blue = -1;
  if (NILP (color_name))
    return dflt;
  CHECK_STRING (color_name);
  if (!ohos_defined_color (f, SSDATA (color_name), &color, true, true))
    signal_error ("Undefined color", color_name);
  return color.pixel;
}

/* ------------------------------------------------------------------ */
/* Window title.                                                       */

static void
ohos_set_name_internal (struct frame *f, Lisp_Object name)
{
  Lisp_Object encoded;
  const char *text;

  if (!FRAME_LIVE_P (f))
    return;
  encoded = ENCODE_UTF_8 (name);
  text = SSDATA (encoded);
  ohos_set_title (text);
}

static void
ohos_set_name (struct frame *f, Lisp_Object name, bool explicit)
{
  if (explicit)
    {
      if (f->explicit_name && NILP (name))
	update_mode_lines = 37;
      f->explicit_name = !NILP (name);
    }
  else if (f->explicit_name)
    return;

  if (NILP (name))
    name = build_string ("GNU Emacs");
  else
    CHECK_STRING (name);

  fset_name (f, name);
  ohos_set_name_internal (f, name);
}

/* ------------------------------------------------------------------ */
/* Default font.                                                       */

/* Set up the default face font for frame F from PARMS.  Mirrors
   android_default_font_parameter: an explicit `font' frame parameter is
   opened through the normal font machinery, otherwise the built-in
   OHOS default font is used.  */

void
ohos_default_font_parameter (struct frame *f, Lisp_Object parms)
{
  Lisp_Object font = Qnil;

  if (NILP (Fassq (Qfont, parms)))
    {
      Lisp_Object value
	= gui_display_get_arg (FRAME_DISPLAY_INFO (f), parms, Qfont, "font",
			       "Font", RES_TYPE_STRING);
      if (!BASE_EQ (value, Qunbound))
	parms = Fcons (Fcons (Qfont, value), parms);
      if (!NILP (value) && STRINGP (value))
	font = font_open_by_name (f, value);
    }
  else
    {
      Lisp_Object value = Fassq (Qfont, parms);
      if (!NILP (value) && STRINGP (XCDR (value)))
	font = font_open_by_name (f, XCDR (value));
      else if (!NILP (value) && FONTP (XCDR (value)))
	font = XCDR (value);
    }

  if (NILP (font))
    font = ohosfont_make_default_object (f);
  if (NILP (font))
    error ("No suitable font was found");

  gui_default_parameter (f, parms, Qfont, font, "font", "Font",
			 RES_TYPE_STRING);
}

/* ------------------------------------------------------------------ */
/* Frame parameter setters.                                            */

static void
ohos_set_background_color (struct frame *f, Lisp_Object arg,
			   Lisp_Object oldval)
{
  unsigned long bg = ohos_decode_color (f, arg, WHITE_PIX_DEFAULT (f));

  FRAME_BACKGROUND_PIXEL (f) = bg;
  update_face_from_frame_parameter (f, Qbackground_color, arg);
  if (FRAME_VISIBLE_P (f))
    redraw_frame (f);
}

static void
ohos_set_foreground_color (struct frame *f, Lisp_Object arg,
			   Lisp_Object oldval)
{
  unsigned long fg = ohos_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  unsigned long old_fg = FRAME_FOREGROUND_PIXEL (f);

  FRAME_FOREGROUND_PIXEL (f) = fg;
  if (FRAME_OUTPUT_DATA (f)->cursor_pixel == old_fg)
    FRAME_OUTPUT_DATA (f)->cursor_pixel = fg;
  update_face_from_frame_parameter (f, Qforeground_color, arg);
  if (FRAME_VISIBLE_P (f))
    redraw_frame (f);
}

static void
ohos_set_cursor_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  unsigned long fore_pixel, pixel;
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);

  if (!NILP (Vx_cursor_fore_pixel))
    fore_pixel = ohos_decode_color (f, Vx_cursor_fore_pixel,
				    WHITE_PIX_DEFAULT (f));
  else
    fore_pixel = FRAME_BACKGROUND_PIXEL (f);

  pixel = ohos_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  /* Make sure the cursor stands out from the background.  */
  if (pixel == FRAME_BACKGROUND_PIXEL (f))
    {
      pixel = FRAME_FOREGROUND_PIXEL (f);
      if (pixel == fore_pixel)
	fore_pixel = FRAME_BACKGROUND_PIXEL (f);
    }
  o->cursor_foreground_pixel = fore_pixel;
  o->cursor_pixel = pixel;

  update_face_from_frame_parameter (f, Qcursor_color, arg);
  if (FRAME_VISIBLE_P (f))
    {
      gui_update_cursor (f, false);
      gui_update_cursor (f, true);
    }
}

static void
ohos_set_cursor_type (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  set_frame_cursor_types (f, arg);
}

static void
ohos_set_mouse_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  unsigned long pixel = ohos_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  unsigned long mask = FRAME_BACKGROUND_PIXEL (f);

  /* Don't let the pointer become invisible.  */
  if (pixel == mask)
    pixel = FRAME_FOREGROUND_PIXEL (f);
  FRAME_OUTPUT_DATA (f)->mouse_pixel = pixel;
  update_face_from_frame_parameter (f, Qmouse_color, arg);
}

static void
ohos_set_border_color (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  /* The ArkUI window manager draws the decoration; only the Emacs
     internal border is ours to color.  */
  ohos_decode_color (f, arg, BLACK_PIX_DEFAULT (f));
  update_face_from_frame_parameter (f, Qborder_color, arg);
  if (FRAME_VISIBLE_P (f))
    redraw_frame (f);
}

static void
ohos_set_internal_border_width (struct frame *f, Lisp_Object arg,
				Lisp_Object oldval)
{
  int border = check_int_nonnegative (arg);

  {
    char buf[160];
    snprintf (buf, sizeof buf, "set_ibw enter border=%d cur=%d f=%p o=%p",
	      border, FRAME_INTERNAL_BORDER_WIDTH (f), (void *) f,
	      (void *) FRAME_OUTPUT_DATA (f));
    ohos_log (buf);
  }
  if (border != FRAME_INTERNAL_BORDER_WIDTH (f))
    {
      f->internal_border_width = border;
      adjust_frame_size (f, -1, -1, 3, false, Qinternal_border_width);
      ohos_log ("set_ibw adjust done");
      FRAME_TERMINAL (f)->rif->clear_under_internal_border (f);
      ohos_log ("set_ibw clear done");
    }
}

static void
ohos_set_child_frame_border_width (struct frame *f, Lisp_Object arg,
				   Lisp_Object oldval)
{
  int border;

  if (NILP (arg))
    border = -1;
  else if (RANGED_FIXNUMP (0, arg, INT_MAX))
    border = XFIXNAT (arg);
  else
    signal_error ("Invalid child frame border width", arg);

  if (border != FRAME_CHILD_FRAME_BORDER_WIDTH (f))
    {
      f->child_frame_border_width = border;
      adjust_frame_size (f, -1, -1, 3, false, Qchild_frame_border_width);
      FRAME_TERMINAL (f)->rif->clear_under_internal_border (f);
    }
}

/* OHOS frames cannot be reparented: ArkUI owns the window tree.  The
   parameter is validated and recorded so Lisp code that queries it sees
   a consistent value, but no native window operation is performed.  */

static void
ohos_set_parent_frame (struct frame *f, Lisp_Object new_value,
		       Lisp_Object old_value)
{
  struct frame *p = NULL;

  if (!NILP (new_value)
      && (!FRAMEP (new_value)
	  || !FRAME_LIVE_P (p = XFRAME (new_value))
	  || !FRAME_OHOS_P (p)))
    {
      store_frame_param (f, Qparent_frame, old_value);
      error ("Invalid specification of `parent-frame'");
    }
  if (p == f)
    error ("Can't make a frame a parent of itself");
  fset_parent_frame (f, new_value);
}

/* ------------------------------ menu / tool / tab bars ------------ */

static void
ohos_set_menu_bar_lines (struct frame *f, Lisp_Object value,
			 Lisp_Object oldval)
{
  int nlines;
  int olines = FRAME_MENU_BAR_LINES (f);

  if (FRAME_MINIBUF_ONLY_P (f) || FRAME_PARENT_FRAME (f))
    return;

  nlines = TYPE_RANGED_FIXNUMP (int, value) ? XFIXNUM (value) : 0;

  /* Make sure we redisplay all windows in this frame.  */
  fset_redisplay (f);

  /* External menu bar (xfns.c:1708): the frame keeps no glyph row, ArkUI
     draws it.  display_menu_bar returns early for OHOS frames (xdisp.c),
     so redisplay never tries to draw the row itself.  */
  FRAME_MENU_BAR_LINES (f) = 0;
  FRAME_MENU_BAR_HEIGHT (f) = 0;
  FRAME_EXTERNAL_MENU_BAR (f) = nlines ? 1 : 0;

  if (nlines != olines)
    {
      adjust_frame_size (f, -1, -1, 3, true, Qmenu_bar_lines);
      FRAME_TERMINAL (f)->rif->clear_under_internal_border (f);
    }
}

static void
ohos_set_tab_bar_lines (struct frame *f, Lisp_Object value,
			Lisp_Object oldval)
{
  int olines = FRAME_TAB_BAR_LINES (f);
  int nlines;

  if (FRAME_MINIBUF_ONLY_P (f))
    return;
  nlines = RANGED_FIXNUMP (0, value, INT_MAX) ? XFIXNAT (value) : 0;
  if (nlines != olines && (olines == 0 || nlines == 0))
    ohos_change_tab_bar_height (f, nlines * FRAME_LINE_HEIGHT (f));
}

static void
ohos_set_tool_bar_lines (struct frame *f, Lisp_Object value,
			 Lisp_Object oldval)
{
  int nlines;

  if (FRAME_MINIBUF_ONLY_P (f))
    return;
  nlines = RANGED_FIXNUMP (0, value, INT_MAX) ? XFIXNAT (value) : 0;
  ohos_change_tool_bar_height (f, nlines * FRAME_LINE_HEIGHT (f));
}

static void
ohos_set_tool_bar_position (struct frame *f, Lisp_Object new_value,
			    Lisp_Object old_value)
{
  if (!EQ (new_value, Qtop) && !EQ (new_value, Qbottom))
    error ("Tool bar position must be either `top' or `bottom'");
  if (EQ (new_value, old_value))
    return;
  fset_tool_bar_position (f, new_value);
  adjust_frame_size (f, -1, -1, 3, false, Qtool_bar_position);
  adjust_frame_glyphs (f);
  SET_FRAME_GARBAGED (f);
}

/* ------------------------------ geometry -------------------------- */

/* Resize the native ArkUI window of F to WIDTH x HEIGHT pixels.  */

void
ohos_set_window_size (struct frame *f, bool change_gravity,
		      int width, int height)
{
  {
    char buf[160];
    snprintf (buf, sizeof buf,
	      "set_window_size req=%dx%d frame_px=%dx%d cols=%d lines=%d cell=%dx%d",
	      width, height, FRAME_PIXEL_WIDTH (f), FRAME_PIXEL_HEIGHT (f),
	      FRAME_COLS (f), FRAME_LINES (f),
	      FRAME_COLUMN_WIDTH (f), FRAME_LINE_HEIGHT (f));
    ohos_log (buf);
  }
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);
  int dw, dh;

  if (width <= 0 || height <= 0)
    return;
  if (change_gravity)
    f->win_gravity = NorthWestGravity;
  o->width = width;
  o->height = height;

  /* A resize that matches the surface size the ArkUI page reported
     came from the user resizing the window; echoing it back would
     start a resize loop.  */
  /* Before the first surface report the native window already exists
     and owns its size; asking the host to resize it would shrink it to
     the boot frame size.  */
  if (!o->surface_seen)
    return;

  dw = width - o->host_surface_width;
  dh = height - o->host_surface_height;
  if (dw < 0)
    dw = -dw;
  if (dh < 0)
    dh = -dh;
  if (o->host_surface_width > 0 && o->host_surface_height > 0
      && dw <= FRAME_COLUMN_WIDTH (f) && dh <= FRAME_LINE_HEIGHT (f))
    return;

  o->geometry_serial++;
  ohos_host_command ("resize %d %d", width, height);
  ohos_publish_frame_metrics (f);
  ohos_initialize_frame_menubar (f);
}

void
ohos_set_window_size_and_position (struct frame *f, int width, int height)
{
  ohos_set_window_size (f, true, width, height);
}

void
ohos_set_offset (struct frame *f, register int xoff, register int yoff,
		 int change_gravity)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);

  /* -1 means "keep the current value", as for other GUI backends.  */
  if (xoff >= 0)
    {
      f->left_pos = xoff;
      if (change_gravity)
	f->win_gravity = NorthWestGravity;
    }
  if (yoff >= 0)
    {
      f->top_pos = yoff;
      if (change_gravity)
	f->win_gravity = NorthWestGravity;
    }
  if (change_gravity)
    o->geometry_serial++;
  if (xoff >= 0 || yoff >= 0)
    ohos_host_command ("move %d %d", f->left_pos, f->top_pos);
}

static void
ohos_set_alpha (struct frame *f, Lisp_Object arg, Lisp_Object oldval)
{
  double alpha = 1.0;
  double newval[2];
  int i;
  Lisp_Object item;

  for (i = 0; i < 2; i++)
    {
      newval[i] = 1.0;
      if (CONSP (arg))
	{
	  item = CAR (arg);
	  arg = CDR (arg);
	}
      else
	item = arg;

      if (NILP (item))
	alpha = -1.0;
      else if (FLOATP (item))
	{
	  alpha = XFLOAT_DATA (item);
	  if (! (0 <= alpha && alpha <= 1.0))
	    args_out_of_range (make_float (0.0), make_float (1.0));
	}
      else if (FIXNUMP (item))
	{
	  EMACS_INT ialpha = XFIXNUM (item);
	  if (! (0 <= ialpha && ialpha <= 100))
	    args_out_of_range (make_fixnum (0), make_fixnum (100));
	  alpha = ialpha / 100.0;
	}
      else
	wrong_type_argument (Qnumberp, item);
      newval[i] = alpha;
    }

  for (i = 0; i < 2; i++)
    f->alpha[i] = newval[i];

  if (FRAME_TERMINAL (f)->set_frame_alpha_hook)
    {
      block_input ();
      FRAME_TERMINAL (f)->set_frame_alpha_hook (f);
      unblock_input ();
    }
}

static void
ohos_set_no_focus_on_map (struct frame *f, Lisp_Object new_value,
			  Lisp_Object old_value)
{
  if (!EQ (new_value, old_value))
    {
      FRAME_NO_FOCUS_ON_MAP (f) = !NILP (new_value);
      ohos_host_command ("no-focus-on-map %d", !NILP (new_value));
    }
}

static void
ohos_set_no_accept_focus (struct frame *f, Lisp_Object new_value,
			  Lisp_Object old_value)
{
  if (!EQ (new_value, old_value))
    {
      FRAME_NO_ACCEPT_FOCUS (f) = !NILP (new_value);
      ohos_host_command ("no-accept-focus %d", !NILP (new_value));
    }
}

static void
ohos_set_title_param (struct frame *f, Lisp_Object name, Lisp_Object old_name)
{
  if (EQ (name, f->title))
    return;
  update_mode_lines = 38;
  fset_title (f, name);
  if (NILP (name))
    name = f->name;
  else
    CHECK_STRING (name);
  ohos_set_name_internal (f, name);
}

static void
ohos_explicitly_set_name (struct frame *f, Lisp_Object arg,
			  Lisp_Object oldval)
{
  ohos_set_name (f, arg, true);
}

/* Called from redisplay when `frame-title-format' changes.  */

void
ohos_implicitly_set_name (struct frame *f, Lisp_Object arg,
			  Lisp_Object oldval)
{
  ohos_set_name (f, arg, false);
}

/* Install a new default font on F.  */

Lisp_Object
ohos_set_new_font (struct frame *f, Lisp_Object font_object, int fontset)
{
  struct font *font = XFONT_OBJECT (font_object);

  if (fontset < 0)
    fontset = fontset_from_font (font_object);
  FRAME_FONTSET (f) = fontset;
  FRAME_FONT (f) = font;
  FRAME_OUTPUT_DATA (f)->font = font;
  FRAME_OUTPUT_DATA (f)->fontset = fontset;

  /* The host side needs to know the new grid metrics.  */
  ohos_font_metrics (font->ascent, font->descent, font->height,
		     font->space_width, font->underline_position,
		     font->underline_thickness);
  ohos_publish_frame_metrics (f);
  return font_object;
}

/* ------------------------------ fullscreen / visibility ------------ */

void
ohos_fullscreen_hook (struct frame *f)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);
  int fs = f->want_fullscreen;
  int value;

  switch (fs)
    {
    case FULLSCREEN_WIDTH:
    case FULLSCREEN_HEIGHT:
    case FULLSCREEN_BOTH:
    case FULLSCREEN_MAXIMIZED:
      value = 1;
      break;
    default:
      value = 0;
      break;
    }
  o->fullscreen_hint = value;
  ohos_host_command ("fullscreen %d", value);
}

void
ohos_frame_visible_invisible (struct frame *f, bool visible)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);

  o->window_shown = visible;
  ohos_host_command ("window-visible %d", visible ? 1 : 0);
  if (visible)
    {
      SET_FRAME_ICONIFIED (f, false);
      SET_FRAME_GARBAGED (f);
      fset_redisplay (f);
    }
}

void
ohos_iconify_frame (struct frame *f)
{
  if (!FRAME_VISIBLE_P (f))
    return;
  FRAME_OUTPUT_DATA (f)->iconified = 1;
  SET_FRAME_ICONIFIED (f, true);
  SET_FRAME_VISIBLE (f, false);
  ohos_host_command ("minimize");
}

/* ------------------------------ frame lifecycle ------------------- */

static void
ohos_unwind_create_frame (Lisp_Object frame)
{
  /* Keep FRAME rooted while the C code below calls Lisp (fontset and
     face realization can allocate and run the GC).  */
  (void) frame;
}

struct frame *
ohos_create_frame (struct terminal *terminal, Lisp_Object parms)
{
  ohos_log ("ohos_create_frame enter");
  if (!terminal)
    terminal = ohos_find_terminal ();
  if (!terminal)
    error ("OHOS terminal is not initialized");

  specpdl_ref count = SPECPDL_INDEX ();
  struct frame *f = make_frame (true);
  Lisp_Object frame;
  XSETFRAME (frame, f);
  record_unwind_protect (ohos_unwind_create_frame, frame);

  f->terminal = terminal;
  f->output_method = output_ohos;
  f->output_data.ohos = xzalloc (sizeof *f->output_data.ohos);
  f->output_data.ohos->display_info = terminal->display_info.ohos;
  {
    int sw = 0, sh = 0;

    ohos_last_surface_size (&sw, &sh);
    f->output_data.ohos->width = sw > 0 ? sw : 800;
    f->output_data.ohos->height = sh > 0 ? sh : 600;
  }
  f->output_data.ohos->fontset = fs_query_fontset (build_string ("fontset-default"), 0);
  f->output_data.ohos->parent_desc = 0;
  f->output_data.ohos->root_window = 0;
  f->output_data.ohos->cursor_pixel = -1;
  f->output_data.ohos->cursor_foreground_pixel = -1;
  f->output_data.ohos->mouse_pixel = -1;
  f->output_data.ohos->window_shown = true;
  f->output_data.ohos->alpha_hint = 100;
  f->output_data.ohos->complete_p = true;
  ohos_init_frame_cursors (f);

  FRAME_DISPLAY_INFO (f)->resx = 96;
  FRAME_DISPLAY_INFO (f)->resy = 96;
  f->column_width = 9;
  f->line_height = 18;

  FRAME_FOREGROUND_PIXEL (f) = 0x000000;
  FRAME_BACKGROUND_PIXEL (f) = 0xffffff;
  f->vertical_scroll_bar_type = vertical_scroll_bar_none;
  f->horizontal_scroll_bars = false;
  f->can_set_window_size = true;

  ohosfont_register_frame (f);
  /* Enabling the drivers for this frame is what makes face font lookup
     consult the backend: register_font_driver leaves them `off' and only
     the :font-backend frame parameter turns them on (see
     font_update_drivers).  Android's frame creation does the same.  */
  gui_default_parameter (f, parms, Qfont_backend, Qnil,
                         "fontBackend", "FontBackend", RES_TYPE_STRING);
  ohos_default_font_parameter (f, parms);
  if (!FRAME_FONT (f))
    {
      unbind_to (count, Qnil);
      error ("Invalid frame font");
    }
  {
    /* Map the CJK charsets explicitly; fontset_from_font only maps the
       registry's primary charset (unicode-bmp), leaving the generic
       chinese-gb2312 fallback in front for CJK text.  */
    static const char *const cjk_charsets[] = {
      "chinese-gb2312", "chinese-gbk", "chinese-big5-1",
      "chinese-big5-2", "japanese-jisx0208", "korean-ksc5601",
      "unicode-bmp", NULL
    };
    Lisp_Object font_obj = get_frame_param (f, Qfont);
    if (!NILP (font_obj) && FONTP (font_obj))
      {
	Lisp_Object fset_name = fontset_name (FRAME_FONTSET (f));
	Lisp_Object spec = AREF (font_obj, FONT_NAME_INDEX);
	for (int i = 0; cjk_charsets[i]; i++)
	  Fset_fontset_font (fset_name, intern (cjk_charsets[i]),
			     spec, Qnil, Qnil);
      }
  }
  ohos_log ("ohos_create_frame font/fontset done");

  gui_default_parameter (f, parms, Qinternal_border_width, make_fixnum (0),
			 "internalBorderWidth", "internalBorderWidth",
			 RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qchild_frame_border_width, Qnil,
			 "childFrameBorderWidth", "childFrameBorderWidth",
			 RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qright_divider_width, make_fixnum (0),
			 NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qbottom_divider_width, make_fixnum (0),
			 NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qvertical_scroll_bars, Qright,
			 "verticalScrollBars", "ScrollBars", RES_TYPE_SYMBOL);
  gui_default_parameter (f, parms, Qhorizontal_scroll_bars, Qnil,
			 "horizontalScrollBars", "ScrollBars",
			 RES_TYPE_SYMBOL);
  gui_default_parameter (f, parms, Qforeground_color, build_string ("black"),
			 "foreground", "Foreground", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qbackground_color, build_string ("white"),
			 "background", "Background", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qmouse_color, build_string ("black"),
			 "pointerColor", "Foreground", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qcursor_color, build_string ("black"),
			 "cursorColor", "Foreground", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qborder_color, build_string ("black"),
			 "borderColor", "BorderColor", RES_TYPE_STRING);
  gui_default_parameter (f, parms, Qscreen_gamma, Qnil,
			 "screenGamma", "ScreenGamma", RES_TYPE_FLOAT);
  gui_default_parameter (f, parms, Qline_spacing, Qnil,
			 "lineSpacing", "LineSpacing", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qleft_fringe, Qnil,
			 "leftFringe", "LeftFringe", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qright_fringe, Qnil,
			 "rightFringe", "RightFringe", RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qno_special_glyphs, Qnil,
			 NULL, NULL, RES_TYPE_BOOLEAN);
  /* The default face is realized from the frame parameters, so all the
     color parameters must be in place before init_frame_faces: it calls
     realize_basic_faces, which aborts when they are missing.  */
  init_frame_faces (f);
  ohos_log ("ohos_create_frame faces done");
  /* The window that just got a frame is the foreground window.  Focus
     events from ArkUI can arrive before the frame exists, and without
     `highlight_frame' redisplay draws the inactive (hollow) cursor for
     ever: `get_window_cursor_type' marks a frame inactive when
     `f != FRAME_DISPLAY_INFO (f)->highlight_frame'.  */
  FRAME_DISPLAY_INFO (f)->focus_frame = frame;
  FRAME_DISPLAY_INFO (f)->highlight_frame = f;



  gui_default_parameter (f, parms, Qmenu_bar_lines,
			 NILP (Vmenu_bar_mode) ? make_fixnum (0) : make_fixnum (1),
			 NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qtab_bar_lines,
			 NILP (Vtab_bar_mode) ? make_fixnum (0) : make_fixnum (1),
			 NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qtool_bar_lines,
			 NILP (Vtool_bar_mode) ? make_fixnum (0) : make_fixnum (1),
			 NULL, NULL, RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qalpha, Qnil, NULL, NULL,
			 RES_TYPE_NUMBER);
  gui_default_parameter (f, parms, Qfullscreen, Qnil, "fullscreen",
			 "Fullscreen", RES_TYPE_SYMBOL);

  /* A name is needed before anything can ask the frame for a title.  */
  {
    Lisp_Object name = Fassq (Qname, parms);
    if (!NILP (name) && STRINGP (XCDR (name)))
      {
	fset_name (f, XCDR (name));
	f->explicit_name = true;
      }
    else
      {
	fset_name (f, build_string ("GNU Emacs"));
	f->explicit_name = false;
      }
  }
  /* adjust_frame_size takes PIXEL dimensions: FRAME_TEXT_TO_PIXEL_WIDTH
     adds the decorations to its argument without multiplying by the
     column width, and xfns.c passes FRAME_COLS * FRAME_COLUMN_WIDTH.
     Passing 80x24 here made the frame 80x24 *pixels* (~10x6 cells), so
     everything was drawn into a tiny corner.  */
  adjust_frame_size (f, 80 * 9, 24 * 18, 5, true, Qnil);
  ohos_publish_frame_metrics (f);
  ohos_log ("ohos_create_frame size done");

  f->terminal->reference_count++;
  Vframe_list = Fcons (frame, Vframe_list);
  SET_FRAME_VISIBLE (f, true);
  Fselect_frame (frame, Qnil);
  SET_FRAME_GARBAGED (f);
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
  /* Realize the standard faces and publish the initial title.  */
  ohos_set_name_internal (f, f->name);
  {
    /* Adopt the geometry the host already reported: the startup screen
       decides between the fancy and plain layouts from (frame-height), so
       the frame must be its real size before Lisp first looks at it.  */
    int sw = 0, sh = 0;

    ohos_last_surface_size (&sw, &sh);
    if (sw > 0 && sh > 0)
      change_frame_size (f, sw, sh, false, false, false);
  }
  unbind_to (count, Qnil);
  return f;
}

DEFUN ("ohos-create-frame", Fohos_create_frame, Sohos_create_frame, 0, 1, 0,
       doc: /* Create a native OHOS frame.
PARMS is an alist of frame parameters.  */)
  (Lisp_Object parms)
{
  if (NILP (parms))
    parms = Qnil;
  struct frame *f = ohos_create_frame (NULL, parms);
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

/* Escape STRING as a JSON string literal (without the quotes).  */

static void
ohos_json_escape (const char *src, char *dst, size_t cap)
{
  static const char hex[] = "0123456789abcdef";
  size_t out = 0;

  for (; *src && out + 7 < cap; src++)
    {
      unsigned char c = (unsigned char) *src;

      switch (c)
	{
	case '"':
	case '\\':
	  dst[out++] = '\\';
	  dst[out++] = c;
	  break;
	case '\n':
	  dst[out++] = '\\';
	  dst[out++] = 'n';
	  break;
	case '\r':
	  dst[out++] = '\\';
	  dst[out++] = 'r';
	  break;
	case '\t':
	  dst[out++] = '\\';
	  dst[out++] = 't';
	  break;
	default:
	  if (c < 0x20)
	    {
	      dst[out++] = '\\';
	      dst[out++] = 'u';
	      dst[out++] = '0';
	      dst[out++] = '0';
	      dst[out++] = hex[c >> 4];
	      dst[out++] = hex[c & 0xf];
	    }
	  else
	    dst[out++] = c;
	  break;
	}
    }
  dst[out] = '\0';
}

DEFUN ("ohos-notify", Fohos_notify, Sohos_notify, 2, 3, 0,
       doc: /* Post a system notification with TITLE and BODY.
Optional THIRD argument is accepted for compatibility and ignored.
The notification is published by the ArkUI host, which owns the
HarmonyOS NotificationKit session.  */)
  (Lisp_Object title, Lisp_Object body, Lisp_Object extra)
{
  char title_buf[1024], body_buf[4096], payload[8192];
  Lisp_Object encoded_title, encoded_body;

  (void) extra;
  CHECK_STRING (title);
  CHECK_STRING (body);
  encoded_title = ENCODE_UTF_8 (title);
  encoded_body = ENCODE_UTF_8 (body);
  ohos_json_escape (SSDATA (encoded_title), title_buf, sizeof title_buf);
  ohos_json_escape (SSDATA (encoded_body), body_buf, sizeof body_buf);
  snprintf (payload, sizeof payload, "{\"title\":\"%s\",\"text\":\"%s\"}",
	    title_buf, body_buf);
  ohos_host_command ("notify %s", payload);
  return Qnil;
}

DEFUN ("ohos-battery", Fohos_battery, Sohos_battery, 0, 0, 0,
       doc: /* Return battery state reported by the ArkUI host as JSON.
Value is a string such as
 \"{\\\"capacity\\\":87,\\\"status\\\":2,\\\"plugged\\\":1,\\\"temperature\\\":300}\",
or nil when no host is available or the query timed out.  */)
  (void)
{
  static int next_battery_id = 0x20000000;
  char text[1024];
  int id = next_battery_id++;

  if (!ohos_gui_available ())
    return Qnil;
  if (next_battery_id < 0)
    next_battery_id = 0x20000000;
  ohos_host_command ("battery %d", id);
  text[0] = '\0';
  if (!ohos_host_wait_result (id, 3000, NULL, text, sizeof text))
    return Qnil;
  if (text[0] == '\0')
    return Qnil;
  return build_string (text);
}

DEFUN ("ohos-frame-host-operation", Fohos_frame_host_operation,
       Sohos_frame_host_operation, 1, 1, 0,
       doc: /* Send COMMAND to the ArkUI host.
COMMAND is a short text command understood by the ArkUI page, for
example \"resize 640 480\" or \"notify hello\".  The host applies it to
the native window or to a system service.  */)
  (Lisp_Object command)
{
  CHECK_STRING (command);
  ohos_host_command ("%s", SSDATA (command));
  return Qnil;
}

/* ------------------------------------------------------------------ */
/* Frame parameter handler table.                                      */

/* Must match the order of `frame_parms' in frame.c.  */

frame_parm_handler ohos_frame_parm_handlers[] =
{
  gui_set_autoraise,
  gui_set_autolower,
  ohos_set_background_color,
  ohos_set_border_color,
  gui_set_border_width,
  ohos_set_cursor_color,
  ohos_set_cursor_type,
  gui_set_font,
  ohos_set_foreground_color,
  NULL, /* icon-name */
  NULL, /* icon-type */
  ohos_set_child_frame_border_width,
  ohos_set_internal_border_width,
  gui_set_right_divider_width,
  gui_set_bottom_divider_width,
  ohos_set_menu_bar_lines,
  ohos_set_mouse_color,
  ohos_explicitly_set_name,
  gui_set_scroll_bar_width,
  gui_set_scroll_bar_height,
  ohos_set_title_param,
  gui_set_unsplittable,
  gui_set_vertical_scroll_bars,
  gui_set_horizontal_scroll_bars,
  gui_set_visibility,
  ohos_set_tab_bar_lines,
  ohos_set_tool_bar_lines,
  NULL, /* scroll-bar-foreground */
  NULL, /* scroll-bar-background */
  gui_set_screen_gamma,
  gui_set_line_spacing,
  gui_set_left_fringe,
  gui_set_right_fringe,
  NULL, /* wait-for-wm */
  gui_set_fullscreen,
  gui_set_font_backend,
  ohos_set_alpha,
  NULL, /* sticky */
  ohos_set_tool_bar_position,
  NULL, /* inhibit-double-buffering */
  NULL, /* undecorated */
  ohos_set_parent_frame,
  NULL, /* skip-taskbar */
  ohos_set_no_focus_on_map,
  ohos_set_no_accept_focus,
  NULL, /* z-group */
  NULL, /* override-redirect */
  gui_set_no_special_glyphs,
  gui_set_alpha_background,
  gui_set_borders_respect_alpha_background,
  NULL, /* use-frame-synchronization */
  NULL, /* display */
  NULL, /* left */
  NULL, /* top */
  NULL, /* width */
  NULL, /* height */
  NULL, /* parent-id */
  NULL, /* window-id */
  NULL, /* outer-window-id */
  NULL, /* explicit-name */
  NULL, /* modeline */
  NULL, /* buffer-list */
  NULL, /* buried-buffer-list */
};

DEFUN ("ohos-debug-rif", Fohos_debug_rif, Sohos_debug_rif, 0, 0, 0,
       doc: /* Return backend pointers, for debugging the redisplay interface.  */)
  (void)
{
  struct frame *f = SELECTED_FRAME ();
  struct terminal *t = f ? f->terminal : NULL;
  struct redisplay_interface *rif = t ? t->rif : NULL;
  return list (make_fixnum (f != NULL),
	       make_pointer_integer (rif),
	       make_pointer_integer (rif ? rif->frame_parm_handlers : NULL),
	       make_pointer_integer (FRAME_TERMINAL (f) == t ? t : NULL),
	       make_fixnum (countof (ohos_frame_parm_handlers)),
	       make_fixnum (t ? t->type : -1));
}

void
ohos_frame_parm_init (void)
{
  eassert (countof (ohos_frame_parm_handlers) == 63);
}

/* Absolute pixel position of the pointer.

   `mouse-drag-line' (mode line, divider, header line dragging) asks for
   it through `mouse-absolute-pixel-position', which dispatches on the
   frame type in frame.el.  Without an OHOS entry that function returned
   the constant (0 . 0), so every drag computed a zero displacement and
   the boundary never moved.  Mirrors androidfns.c.  */

DEFUN ("ohos-mouse-absolute-pixel-position",
       Fohos_mouse_absolute_pixel_position,
       Sohos_mouse_absolute_pixel_position, 0, 0, 0,
       doc: /* Return the pointer position in pixels as (X . Y).  */)
  (void)
{
  struct frame *f = SELECTED_FRAME ();
  struct ohos_display_info *dpyinfo;

  if (!f || !FRAME_OHOS_P (f))
    return Fcons (make_fixnum (0), make_fixnum (0));
  dpyinfo = FRAME_DISPLAY_INFO (f);
  return Fcons (make_fixnum (dpyinfo->last_mouse_motion_x),
		make_fixnum (dpyinfo->last_mouse_motion_y));
}

/* RGB lookup for a colour name on an OHOS frame.

   `frame--current-background-mode' (used by `frame-set-background-mode' and
   therefore by every face spec that says `(background light)') calls
   `xw-color-values' to measure the frame background.  The subr did not exist
   for this backend, the Lisp signalled `void-function', and every face kept
   the `(t :inverse-video t)' fallback: no colour on hover and no mode-line
   box (B-17).  Mirrors androidfns.c.  */

DEFUN ("xw-color-values", Fxw_color_values, Sxw_color_values, 1, 2, 0,
       doc: /* Return the RGB values of COLOR on FRAME as 16-bit integers.  */)
  (Lisp_Object color, Lisp_Object frame)
{
  Emacs_Color c;
  struct frame *f = decode_window_system_frame (frame);

  CHECK_STRING (color);
  if (ohos_defined_color (f, SSDATA (color), &c, false, false))
    return list3i (c.red, c.green, c.blue);
  return Qnil;
}

DEFUN ("xw-color-defined-p", Fxw_color_defined_p, Sxw_color_defined_p,
       1, 2, 0,
       doc: /* Return t if COLOR is defined on FRAME.  */)
  (Lisp_Object color, Lisp_Object frame)
{
  Emacs_Color c;
  struct frame *f = decode_window_system_frame (frame);

  CHECK_STRING (color);
  return ohos_defined_color (f, SSDATA (color), &c, false, false) ? Qt : Qnil;
}

/* The colour / display-capability predicates every GUI backend provides.
   Without them `display-color-p', `display-color-cells' and
   `frame-set-background-mode' cannot classify an OHOS frame, so every
   `(class color)' / `(min-colors 88)' / `(background light)' face spec was
   skipped and faces such as `highlight' stayed unspecified (task book
   B-15).  Mirrors androidfns.c.  */

static struct ohos_display_info *
ohos_check_display_info (Lisp_Object object)
{
  struct frame *f;
  struct terminal *t;

  if (NILP (object))
    f = SELECTED_FRAME ();
  else if (FRAMEP (object))
    f = XFRAME (object);
  else
    f = NULL;

  if (f && FRAME_LIVE_P (f) && FRAME_OHOS_P (f))
    return FRAME_DISPLAY_INFO (f);

  for (t = terminal_list; t; t = t->next_terminal)
    if (t->type == output_ohos && t->display_info.ohos)
      return t->display_info.ohos;

  error ("No OHOS display found");
  return NULL;
}

/* globals.h is generated by make-docfile, which cannot run for this
   cross build, so new subrs need their own forward declaration.  */
Lisp_Object Fohos_display_planes (Lisp_Object);

DEFUN ("x-display-planes", Fohos_display_planes,
       Sohos_display_planes, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  return make_fixnum (ohos_check_display_info (terminal)->n_planes);
}

DEFUN ("x-display-color-cells", Fx_display_color_cells,
       Sx_display_color_cells, 0, 1, 0,
       doc: /* SKIP: real doc in xfns.c.  */)
  (Lisp_Object terminal)
{
  int nr_planes = ohos_check_display_info (terminal)->n_planes;

  if (nr_planes > 24)
    nr_planes = 24;
  return make_fixnum (1 << nr_planes);
}

void
syms_of_ohosfns (void)
{
  defsubr (&Sohos_create_frame);
  defsubr (&Sohos_display_planes);
  defsubr (&Sohos_set_title);
  defsubr (&Sohos_set_clipboard);
  defsubr (&Sohos_frame_host_operation);
  defsubr (&Sohos_notify);
  defsubr (&Sohos_battery);
  defsubr (&Sohos_debug_rif);
  defsubr (&Sohos_mouse_absolute_pixel_position);
  defsubr (&Sxw_color_values);
  defsubr (&Sxw_color_defined_p);
  defsubr (&Sx_display_color_cells);

  DEFVAR_LISP ("x-cursor-fore-pixel", Vx_cursor_fore_pixel,
	       doc: /* Pixel color of the text cursor foreground.
A string containing the color name, or nil to use the frame background
color.  */);
  Vx_cursor_fore_pixel = Qnil;
}
