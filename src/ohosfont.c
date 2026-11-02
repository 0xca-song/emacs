/* OHOS fixed-cell font driver skeleton.
   This is the Emacs-side font driver for the native OHOS frame backend.
   It intentionally uses a fixed 9x18 cell so the first native frame can
   be validated before a full FreeType/HarfBuzz backend is added.  */

#include <config.h>
typedef void *Emacs_Pix_Container;
typedef void *Emacs_Cursor;
typedef void *Emacs_Pixmap;
typedef unsigned long RGB_PIXEL_COLOR;
typedef struct ohos_display_info Display_Info;
#include "lisp.h"
#include "frame.h"
#include "dispextern.h"
#include "font.h"
#include "character.h"
#include "ohosgui.h"

#include <string.h>
#include <stdio.h>
#include <native_drawing/drawing_font.h>
#include <native_drawing/drawing_font_mgr.h>
#include <native_drawing/drawing_typeface.h>
#include <native_drawing/drawing_text_declaration.h>

struct ohosfont_info
{
  struct font font;
  OH_Drawing_Font *drawing_font;
  OH_Drawing_Font *cjk_font;
};

static int
ohosfont_utf8 (unsigned code, char *buf)
{
  if (code < 0x80) { buf[0] = (char) code; return 1; }
  if (code < 0x800)
    {
      buf[0] = (char) (0xc0 | (code >> 6));
      buf[1] = (char) (0x80 | (code & 0x3f));
      return 2;
    }
  if (code < 0x10000)
    {
      buf[0] = (char) (0xe0 | (code >> 12));
      buf[1] = (char) (0x80 | ((code >> 6) & 0x3f));
      buf[2] = (char) (0x80 | (code & 0x3f));
      return 3;
    }
  buf[0] = (char) (0xf0 | (code >> 18));
  buf[1] = (char) (0x80 | ((code >> 12) & 0x3f));
  buf[2] = (char) (0x80 | ((code >> 6) & 0x3f));
  buf[3] = (char) (0x80 | (code & 0x3f));
  return 4;
}

static struct font_driver ohosfont_driver;
static int ohos_ascent = 14;
static int ohos_descent = 4;
static int ohos_height = 18;
static int ohos_space_width = 9;
static int ohos_underline_position = 15;
static int ohos_underline_thickness = 1;

void
ohos_font_metrics (int ascent, int descent, int height, int space_width,
		   int underline_position, int underline_thickness)
{
  int total = ascent + descent;
  if (total <= 0)
    return;
  ohos_height = height > 0 ? height : 18;
  ohos_ascent = (ohos_height * ascent + total / 2) / total;
  if (ohos_ascent >= ohos_height)
    ohos_ascent = ohos_height - 1;
  ohos_descent = ohos_height - ohos_ascent;
  if (space_width > 0)
    ohos_space_width = space_width;
  if (underline_position >= 0)
    ohos_underline_position = underline_position;
  if (underline_thickness > 0)
    ohos_underline_thickness = underline_thickness;
}

static Lisp_Object
ohosfont_list (struct frame *f, Lisp_Object font_spec)
{
  Lisp_Object entity = font_make_entity ();
  ASET (entity, FONT_TYPE_INDEX, Qohosfont);
  ASET (entity, FONT_FOUNDRY_INDEX, Qohosfont);
  ASET (entity, FONT_FAMILY_INDEX, intern ("Noto Sans Mono"));
  ASET (entity, FONT_ADSTYLE_INDEX, Qnil);
  ASET (entity, FONT_REGISTRY_INDEX, Qiso10646_1);
  ASET (entity, FONT_SIZE_INDEX, make_fixnum (0));
  ASET (entity, FONT_AVGWIDTH_INDEX, make_fixnum (0));
  ASET (entity, FONT_SPACING_INDEX, make_fixnum (FONT_SPACING_MONO));
  return list1 (entity);
}

static Lisp_Object
ohosfont_match (struct frame *f, Lisp_Object font_spec)
{
  return ohosfont_list (f, font_spec);
}

static Lisp_Object
ohosfont_list_family (struct frame *f)
{
  return list1 (intern ("Noto Sans Mono"));
}

static Lisp_Object
ohosfont_open (struct frame *f, Lisp_Object entity, int pixel_size)
{
  Lisp_Object obj;
  struct ohosfont_info *info;
  struct font *font;

  if (pixel_size <= 0)
    pixel_size = 12;

  obj = font_make_object (VECSIZE (struct ohosfont_info), entity, pixel_size);
  info = (struct ohosfont_info *) XFONT_OBJECT (obj);
  font = &info->font;
  font->driver = &ohosfont_driver;
  font->pixel_size = pixel_size;
  font->height = ohos_height;
  font->ascent = ohos_ascent;
  font->descent = ohos_descent;
  font->space_width = ohos_space_width;
  font->min_width = 9;
  font->max_width = 18;
  font->average_width = ohos_space_width;
  font->underline_position = ohos_underline_position;
  font->underline_thickness = ohos_underline_thickness;
  font->default_ascent = ohos_ascent;
  font->vertical_centering = 0;
  font->baseline_offset = 0;
  font->relative_compose = 0;
  info->drawing_font = OH_Drawing_FontCreate ();
  OH_Drawing_FontMgr *mgr = OH_Drawing_FontMgrCreate ();
  OH_Drawing_FontStyleStruct style = { FONT_WEIGHT_400, FONT_WIDTH_NORMAL,
				       FONT_STYLE_NORMAL };
  OH_Drawing_Typeface *typeface = mgr
    ? OH_Drawing_FontMgrMatchFamilyStyle (mgr, "monospace", style) : NULL;
  if (typeface && info->drawing_font)
    {
      OH_Drawing_FontSetTypeface (info->drawing_font, typeface);
      OH_Drawing_FontSetTextSize (info->drawing_font, 15.5f);
    }
  if (mgr)
    OH_Drawing_FontMgrDestroy (mgr);
  info->cjk_font = OH_Drawing_FontCreate ();
  OH_Drawing_Typeface *cjk_typeface = OH_Drawing_TypefaceCreateFromFile (
    "/system/fonts/HarmonyOS_Sans_SC.ttf", 0);
  if (!cjk_typeface)
    cjk_typeface = OH_Drawing_TypefaceCreateFromFile (
      "/system/fonts/NotoSansCJK-Regular.ttc", 0);
  if (cjk_typeface && info->cjk_font)
    {
      OH_Drawing_FontSetTypeface (info->cjk_font, cjk_typeface);
      OH_Drawing_FontSetTextSize (info->cjk_font, 18.0f);
    }
  font->props[FONT_NAME_INDEX] = build_string ("Noto Sans Mono-12");
  font->props[FONT_FAMILY_INDEX] = intern ("Noto Sans Mono");
  font->props[FONT_REGISTRY_INDEX] = Qiso10646_1;
  return obj;
}

struct font *
ohosfont_make_default (struct frame *f)
{
  Lisp_Object entity = font_make_entity ();
  ASET (entity, FONT_TYPE_INDEX, Qohosfont);
  ASET (entity, FONT_FOUNDRY_INDEX, Qohosfont);
  ASET (entity, FONT_FAMILY_INDEX, intern ("Noto Sans Mono"));
  ASET (entity, FONT_ADSTYLE_INDEX, Qnil);
  ASET (entity, FONT_REGISTRY_INDEX, Qiso10646_1);
  ASET (entity, FONT_SIZE_INDEX, make_fixnum (12));
  ASET (entity, FONT_AVGWIDTH_INDEX, make_fixnum (0));
  ASET (entity, FONT_SPACING_INDEX, make_fixnum (FONT_SPACING_MONO));
  Lisp_Object obj = ohosfont_open (f, entity, 12);
  return NILP (obj) ? NULL : XFONT_OBJECT (obj);
}

static void
ohosfont_close (struct font *font)
{
  struct ohosfont_info *info = (struct ohosfont_info *) font;
  if (info->drawing_font)
    OH_Drawing_FontDestroy (info->drawing_font);
  if (info->cjk_font)
    OH_Drawing_FontDestroy (info->cjk_font);
  info->drawing_font = NULL;
  info->cjk_font = NULL;
}

static bool ohosfont_wide (unsigned code);

static int
ohosfont_has_char (Lisp_Object font, int c)
{
  struct ohosfont_info *info = (struct ohosfont_info *) XFONT_OBJECT (font);
  if (c < 0 || c > 0x10ffff)
    return 0;
  char buf[4]; int n = ohosfont_utf8 ((unsigned) c, buf);
  uint16_t glyph = 0;
  OH_Drawing_Font *font_obj = ohosfont_wide ((unsigned) c)
    ? info->cjk_font : info->drawing_font;
  if (ohosfont_wide ((unsigned) c))
    {
      char msg[96];
      snprintf (msg, sizeof msg, "has_char U+%04X cjk=%p mono=%p",
		(unsigned) c, info->cjk_font, info->drawing_font);
      ohos_log (msg);
    }
  if (!font_obj)
    font_obj = info->drawing_font ? info->drawing_font : info->cjk_font;
  if (!font_obj)
    return 0;
  if (OH_Drawing_FontTextToGlyphs (font_obj, buf, n, TEXT_ENCODING_UTF8,
				   &glyph, 1) > 0 && glyph != 0)
    return 1;
  /* Fallback between mono and CJK for punctuation/rare characters.  */
  font_obj = font_obj == info->cjk_font ? info->drawing_font : info->cjk_font;
  if (!font_obj)
    return 0;
  glyph = 0;
  return OH_Drawing_FontTextToGlyphs (font_obj, buf, n, TEXT_ENCODING_UTF8,
				      &glyph, 1) > 0 && glyph != 0;
}

static unsigned
ohosfont_encode_char (struct font *font, int c)
{
  struct ohosfont_info *info = (struct ohosfont_info *) font;
  if (c < 0 || c > 0x10ffff)
    return 0;
  char buf[4]; int n = ohosfont_utf8 ((unsigned) c, buf);
  uint16_t glyph = 0;
  OH_Drawing_Font *font_obj = ohosfont_wide ((unsigned) c)
    ? info->cjk_font : info->drawing_font;
  if (ohosfont_wide ((unsigned) c))
    {
      char msg[96];
      snprintf (msg, sizeof msg, "has_char U+%04X cjk=%p mono=%p",
		(unsigned) c, info->cjk_font, info->drawing_font);
      ohos_log (msg);
    }
  if (!font_obj)
    return 0;
  if (OH_Drawing_FontTextToGlyphs (font_obj, buf, n, TEXT_ENCODING_UTF8,
				   &glyph, 1) > 0 && glyph != 0)
    return glyph;
  font_obj = font_obj == info->cjk_font ? info->drawing_font : info->cjk_font;
  if (!font_obj)
    return 0;
  glyph = 0;
  if (OH_Drawing_FontTextToGlyphs (font_obj, buf, n, TEXT_ENCODING_UTF8,
				   &glyph, 1) == 0)
    return 0;
  return glyph;
}

static bool
ohosfont_wide (unsigned code)
{
  return (code >= 0x1100 && code <= 0x115f)
    || (code >= 0x2e80 && code <= 0xa4cf)
    || (code >= 0xac00 && code <= 0xd7a3)
    || (code >= 0xf900 && code <= 0xfaff)
    || (code >= 0xfe30 && code <= 0xfe4f)
    || (code >= 0xff00 && code <= 0xff60)
    || (code >= 0xffe0 && code <= 0xffe6)
    || (code >= 0x20000 && code <= 0x3fffd);
}

static void
ohosfont_text_extents (struct font *font, const unsigned *code, int nglyphs,
                       struct font_metrics *metrics)
{
  struct ohosfont_info *info = (struct ohosfont_info *) font;
  int width = 0;
  for (int i = 0; i < nglyphs; i++)
    {
      if (ohosfont_wide (code[i]))
	width += 18;
      else
	{
	  float advance = 0.0f;
	  char buf[4];
	  int n = ohosfont_utf8 (code[i], buf);
	  if (info->drawing_font
	      && OH_Drawing_FontMeasureText (info->drawing_font, buf, n,
					     TEXT_ENCODING_UTF8, NULL,
					     &advance) == OH_DRAWING_SUCCESS)
	    width += (int) (advance + 0.5f);
	  else
	    width += 9;
	}
    }
  metrics->lbearing = 0;
  metrics->rbearing = width;
  metrics->width = width;
  metrics->ascent = font->ascent;
  metrics->descent = font->descent;
}

static int
ohosfont_draw (struct glyph_string *s, int from, int to, int x, int y,
               bool with_background)
{
  if (!s || from >= to || !s->face)
    return 0;
  int line_height = FRAME_LINE_HEIGHT (s->f);
  for (int i = from; i < to; i++)
    {
      struct glyph *g = s->first_glyph + i;
      if (g->type != CHAR_GLYPH)
        continue;
      int w = g->pixel_width > 0 ? g->pixel_width : FRAME_COLUMN_WIDTH (s->f);
      if (w <= 0)
        continue;
      uint32_t flags = 0;
      if (with_background
          && s->face->background != s->face->foreground)
        flags |= 1;
      if (s->face->underline != FACE_NO_UNDERLINE)
        flags |= 4;
      if (s->font && FONT_WEIGHT_NUMERIC (s->font) >= 150)
        flags |= 2;
      if (s->face->strike_through_p)
        flags |= 8;
      if (s->face->overline_p)
        flags |= 16;
      ohos_draw_text (x, y, w, line_height, g->u.ch,
                      (uint32_t) s->face->foreground,
                      (uint32_t) s->face->background, flags);
      x += w;
    }
  return 0;
}

static struct font_driver ohosfont_driver =
  {
    .type = 0,
    .case_sensitive = false,
    .list = ohosfont_list,
    .match = ohosfont_match,
    .list_family = ohosfont_list_family,
    .open_font = ohosfont_open,
    .close_font = ohosfont_close,
    .has_char = ohosfont_has_char,
    .encode_char = ohosfont_encode_char,
    .text_extents = ohosfont_text_extents,
    .draw = ohosfont_draw,
  };

void
syms_of_ohosfont (void)
{
  DEFSYM (Qohosfont, "ohos");
  ohosfont_driver.type = Qohosfont;
  register_font_driver (&ohosfont_driver, NULL);
}
