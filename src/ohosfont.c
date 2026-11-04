/* OHOS font driver for the native OHOS GUI backend.

   The driver talks to the platform through OH_Drawing: the system
   font manager is used to enumerate font families and to resolve the
   typefaces of the family and style requested by a font spec, while
   each opened font keeps one font object for its own family and one
   for wide (CJK) characters.  Glyph advances are measured with
   OH_Drawing_FontGetWidths; when the platform cannot answer, the
   driver falls back to the fixed 9x18 cell grid used by the native
   renderer.  */

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
#include "composite.h"
#include "character.h"
#include "ohosgui.h"

#include <string.h>
#include <native_drawing/drawing_font.h>
#include <native_drawing/drawing_font_mgr.h>
#include <native_drawing/drawing_typeface.h>
#include <native_drawing/drawing_text_declaration.h>

/* The nominal family of the driver's default font.  When the platform
   does not have it, `open_font' falls back to the generic "monospace"
   alias and finally to the default typeface.  */
#define OHOSFONT_DEFAULT_FAMILY "Noto Sans Mono"

/* Text sizes used for the two fonts of an opened font object.  They
   are chosen so that the advances match the 9x18 cell grid of the
   native renderer.  */
#define OHOSFONT_TEXT_SIZE 15.5f
#define OHOSFONT_CJK_TEXT_SIZE 18.0f

/* A font style offered for each family.  */
struct ohosfont_style
{
  const char *weight;
  const char *slant;
  const char *width;
  OH_Drawing_FontStyleStruct drawing;
};

struct ohosfont_info
{
  struct font font;
  OH_Drawing_Font *drawing_font;
  OH_Drawing_Font *cjk_font;
  OH_Drawing_Typeface *drawing_typeface;
  OH_Drawing_Typeface *cjk_typeface;
};

/* The styles enumerated by `list'.  */
static const struct ohosfont_style ohosfont_styles[] =
  {
    { "regular", "normal", "normal",
      { FONT_WEIGHT_400, FONT_WIDTH_NORMAL, FONT_STYLE_NORMAL } },
    { "bold", "normal", "normal",
      { FONT_WEIGHT_700, FONT_WIDTH_NORMAL, FONT_STYLE_NORMAL } },
    { "regular", "italic", "normal",
      { FONT_WEIGHT_400, FONT_WIDTH_NORMAL, FONT_STYLE_ITALIC } },
    { "bold", "italic", "normal",
      { FONT_WEIGHT_700, FONT_WIDTH_NORMAL, FONT_STYLE_ITALIC } },
  };

static struct font_driver ohosfont_driver;
static Lisp_Object ohosfont_cache;

static int ohos_ascent = 14;
static int ohos_descent = 4;
static int ohos_height = 18;
static int ohos_space_width = 9;
static int ohos_underline_position = 15;
static int ohos_underline_thickness = 1;

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

/* Return true if CODE is a wide (typically CJK) character that should
   be displayed with the CJK font.  */

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

/* Return the OH_Drawing font of INFO that is preferred for CODE.  */

static OH_Drawing_Font *
ohosfont_pick_font (struct ohosfont_info *info, unsigned code)
{
  if (ohosfont_wide (code) && info->cjk_font)
    return info->cjk_font;
  if (info->drawing_font)
    return info->drawing_font;
  return info->cjk_font;
}

/* Return the glyph code of CODE in DRAWING_FONT, or 0 if that font
   has no glyph for it.  */

static unsigned
ohosfont_glyph_code (OH_Drawing_Font *drawing_font, unsigned code)
{
  char buf[4];
  uint16_t glyph = 0;
  int n;

  if (!drawing_font || code > 0x10ffff)
    return 0;
  n = ohosfont_utf8 (code, buf);
  if (OH_Drawing_FontTextToGlyphs (drawing_font, buf, n, TEXT_ENCODING_UTF8,
                                   &glyph, 1) == 0)
    return 0;
  return glyph;
}

/* Return the glyph code of CODE in INFO, trying the fallback font when
   the preferred one has no glyph.  The value is 0 when neither font
   can display CODE.  */

static unsigned
ohosfont_char_glyph (struct ohosfont_info *info, unsigned code)
{
  OH_Drawing_Font *drawing_font = ohosfont_pick_font (info, code);
  unsigned glyph;

  glyph = ohosfont_glyph_code (drawing_font, code);
  if (glyph == 0)
    {
      drawing_font = (drawing_font == info->cjk_font
                      ? info->drawing_font : info->cjk_font);
      if (drawing_font)
        glyph = ohosfont_glyph_code (drawing_font, code);
    }
  return glyph;
}

/* Return the advance width of CODE in DRAWING_FONT, in pixels, or -1
   if that font has no glyph for CODE.  */

static int
ohosfont_glyph_advance (OH_Drawing_Font *drawing_font, unsigned code)
{
  uint16_t glyph;
  float advance = 0.0f;

  glyph = (uint16_t) ohosfont_glyph_code (drawing_font, code);
  if (glyph == 0)
    return -1;
  OH_Drawing_FontGetWidths (drawing_font, &glyph, 1, &advance);
  if (advance < 0.0f)
    return -1;
  return (int) (advance + 0.5f);
}

/* Return the advance width of CODE in INFO, trying the fallback font
   when the preferred one has no glyph.  The value is -1 when neither
   font can display CODE.  */

static int
ohosfont_char_width (struct ohosfont_info *info, unsigned code)
{
  OH_Drawing_Font *drawing_font = ohosfont_pick_font (info, code);
  OH_Drawing_Font *fallback;
  int width;

  if (!drawing_font)
    return -1;
  width = ohosfont_glyph_advance (drawing_font, code);
  if (width < 0)
    {
      fallback = (drawing_font == info->cjk_font
                  ? info->drawing_font : info->cjk_font);
      if (fallback && fallback != drawing_font)
        width = ohosfont_glyph_advance (fallback, code);
    }
  return width;
}

/* Return true if C is a combining character that is allowed to have a
   zero advance width.  This uses Emacs' own Unicode tables.  */

static bool
ohosfont_combining_p (int c)
{
  static Lisp_Object combining_table;
  static bool table_initialized;
  Lisp_Object value;

  if (!table_initialized)
    {
      combining_table = uniprop_table (Qcanonical_combining_class);
      table_initialized = true;
    }
  if (!CHAR_TABLE_P (combining_table))
    return false;
  value = get_unicode_property (combining_table, c);
  return FIXNUMP (value) && XFIXNUM (value) != 0;
}

/* Return the numeric weight of FONT, or -1 if it is unknown.  */

static int
ohosfont_font_weight (struct font *font)
{
  Lisp_Object weight = font->props[FONT_WEIGHT_INDEX];

  return FIXNUMP (weight) ? (XFIXNUM (weight) >> 8) : -1;
}

static Lisp_Object
ohosfont_default_family (void)
{
  return intern (OHOSFONT_DEFAULT_FAMILY);
}

/* Return the family named by FONT_SPEC, interned, or nil.  */

static Lisp_Object
ohosfont_spec_family (Lisp_Object font_spec)
{
  Lisp_Object family = AREF (font_spec, FONT_FAMILY_INDEX);

  if (SYMBOLP (family) && !NILP (family))
    return family;
  if (STRINGP (family))
    return font_intern_prop (SSDATA (family), SBYTES (family), true);
  return Qnil;
}

/* Return the UTF-8 name of FAMILY, or the driver's default family.  */

static const char *
ohosfont_family_name (Lisp_Object family)
{
  if (SYMBOLP (family) && !NILP (family)
      && SBYTES (SYMBOL_NAME (family)) > 0)
    return SSDATA (SYMBOL_NAME (family));
  return OHOSFONT_DEFAULT_FAMILY;
}

/* Return the OH_Drawing font style described by ENTITY.  */

static OH_Drawing_FontStyleStruct
ohosfont_drawing_style (Lisp_Object entity)
{
  OH_Drawing_FontStyleStruct style
    = { FONT_WEIGHT_400, FONT_WIDTH_NORMAL, FONT_STYLE_NORMAL };
  int weight = FONT_WEIGHT_NUMERIC (entity);
  int slant = FONT_SLANT_NUMERIC (entity);
  int width = FONT_WIDTH_NUMERIC (entity);

  if (weight >= 150)
    style.weight = FONT_WEIGHT_700;
  else if (weight >= 100)
    style.weight = FONT_WEIGHT_500;
  if (slant >= 200)
    style.slant = FONT_STYLE_ITALIC;
  if (width > 0 && width <= 87)
    style.width = FONT_WIDTH_CONDENSED;
  else if (width >= 113)
    style.width = FONT_WIDTH_EXPANDED;
  return style;
}

/* Return true if the system font manager knows FAMILY.  MGR may be
   NULL, in which case the answer is always false.  */

static bool
ohosfont_family_installed (OH_Drawing_FontMgr *mgr, const char *family)
{
  OH_Drawing_FontStyleSet *styles;

  if (!mgr || !family)
    return false;
  styles = OH_Drawing_FontMgrMatchFamily (mgr, family);
  if (!styles)
    return false;
  OH_Drawing_FontMgrDestroyFontStyleSet (styles);
  return true;
}

/* Return true if FAMILY has a typeface for STYLE.  */

static bool
ohosfont_style_installed (OH_Drawing_FontMgr *mgr, const char *family,
                          OH_Drawing_FontStyleStruct style)
{
  OH_Drawing_Typeface *typeface;

  if (!mgr || !family)
    return false;
  typeface = OH_Drawing_FontMgrMatchFamilyStyle (mgr, family, style);
  if (!typeface)
    return false;
  OH_Drawing_TypefaceDestroy (typeface);
  return true;
}

/* Resolve FAMILY and STYLE to a typeface, falling back to the generic
   "monospace" alias.  Return NULL if the platform has neither.  */

static OH_Drawing_Typeface *
ohosfont_match_typeface (const char *family, OH_Drawing_FontStyleStruct style)
{
  OH_Drawing_FontMgr *mgr;
  OH_Drawing_Typeface *typeface = NULL;

  mgr = OH_Drawing_FontMgrCreate ();
  if (!mgr)
    return NULL;
  typeface = OH_Drawing_FontMgrMatchFamilyStyle (mgr, family, style);
  if (!typeface)
    typeface = OH_Drawing_FontMgrMatchFamilyStyle (mgr, "monospace", style);
  OH_Drawing_FontMgrDestroy (mgr);
  return typeface;
}

/* Resolve the typeface used for wide characters.  Prefer a typeface
   of FAMILY that can display a CJK character, then the known CJK font
   files, and finally the system fallback.  */

static OH_Drawing_Typeface *
ohosfont_match_cjk_typeface (const char *family,
                             OH_Drawing_FontStyleStruct style)
{
  static const char *const cjk_files[] =
    {
      "/system/fonts/HarmonyOS_Sans_SC.ttf",
      "/system/fonts/NotoSansCJK-Regular.ttc",
    };
  OH_Drawing_FontMgr *mgr;
  OH_Drawing_Typeface *typeface = NULL;
  int i;

  mgr = OH_Drawing_FontMgrCreate ();
  if (mgr)
    {
      typeface = OH_Drawing_FontMgrMatchFamilyStyleCharacter (
        mgr, family, style, NULL, 0, 0x4e2d);
      if (!typeface)
        typeface = OH_Drawing_FontMgrMatchFamilyStyleCharacter (
          mgr, "monospace", style, NULL, 0, 0x4e2d);
      OH_Drawing_FontMgrDestroy (mgr);
    }
  for (i = 0; !typeface && i < countof (cjk_files); i++)
    typeface = OH_Drawing_TypefaceCreateFromFile (cjk_files[i], 0);
  return typeface;
}

/* Make a font entity for FAMILY and STYLE.  */

static Lisp_Object
ohosfont_make_entity (Lisp_Object family, const struct ohosfont_style *style)
{
  Lisp_Object entity = font_make_entity ();

  if (NILP (family))
    family = ohosfont_default_family ();
  ASET (entity, FONT_TYPE_INDEX, Qohosfont);
  ASET (entity, FONT_FOUNDRY_INDEX, Qohosfont);
  ASET (entity, FONT_FAMILY_INDEX, family);
  ASET (entity, FONT_ADSTYLE_INDEX, Qnil);
  ASET (entity, FONT_REGISTRY_INDEX, Qiso10646_1);
  ASET (entity, FONT_SIZE_INDEX, make_fixnum (0));
  ASET (entity, FONT_AVGWIDTH_INDEX, make_fixnum (0));
  ASET (entity, FONT_SPACING_INDEX, make_fixnum (FONT_SPACING_MONO));
  FONT_SET_STYLE (entity, FONT_WEIGHT_INDEX, intern (style->weight));
  FONT_SET_STYLE (entity, FONT_SLANT_INDEX, intern (style->slant));
  FONT_SET_STYLE (entity, FONT_WIDTH_INDEX, intern (style->width));
  return entity;
}

/* Return the font cache used by the font machinery.  F is ignored;
   the cache is shared by all frames, as it is for the other native
   backends.  */

static Lisp_Object
ohosfont_get_cache (struct frame *f)
{
  (void) f;
  return ohosfont_cache;
}

static Lisp_Object
ohosfont_list (struct frame *f, Lisp_Object font_spec)
{
  Lisp_Object family, result;
  OH_Drawing_FontMgr *mgr;
  const char *name;
  int i;

  (void) f;

  mgr = OH_Drawing_FontMgrCreate ();
  family = ohosfont_spec_family (font_spec);
  name = ohosfont_family_name (family);
  if (!ohosfont_family_installed (mgr, name))
    {
      /* The requested family is not installed.  Keep the driver's
         default monospace family; `open_font' resolves the generic
         "monospace" alias when that is missing as well.  Probe the
         styles through the alias so that bold and italic specs still
         find an entity.  */
      family = ohosfont_default_family ();
      name = OHOSFONT_DEFAULT_FAMILY;
      if (!ohosfont_family_installed (mgr, name))
        name = "monospace";
    }

  result = Qnil;
  for (i = 0; i < countof (ohosfont_styles); i++)
    if (ohosfont_style_installed (mgr, name, ohosfont_styles[i].drawing))
      result = Fcons (ohosfont_make_entity (family, &ohosfont_styles[i]),
                      result);
  if (NILP (result))
    result = list1 (ohosfont_make_entity (family, &ohosfont_styles[0]));

  if (mgr)
    OH_Drawing_FontMgrDestroy (mgr);
  return Fnreverse (result);
}

static Lisp_Object
ohosfont_match (struct frame *f, Lisp_Object font_spec)
{
  Lisp_Object family, entity;
  int i;

  (void) f;

  family = ohosfont_spec_family (font_spec);
  entity = ohosfont_make_entity (family, &ohosfont_styles[0]);
  /* Honor the style requested by the caller; `open_font' turns it
     into an OH_Drawing font style.  */
  for (i = FONT_WEIGHT_INDEX; i <= FONT_WIDTH_INDEX; i++)
    if (FIXNUMP (AREF (font_spec, i)))
      ASET (entity, i, AREF (font_spec, i));
  return entity;
}

static Lisp_Object
ohosfont_list_family (struct frame *f)
{
  OH_Drawing_FontMgr *mgr;
  Lisp_Object families = Qnil;
  int count, i;

  (void) f;

  mgr = OH_Drawing_FontMgrCreate ();
  if (!mgr)
    return list1 (ohosfont_default_family ());

  count = OH_Drawing_FontMgrGetFamilyCount (mgr);
  for (i = 0; i < count; i++)
    {
      char *name = OH_Drawing_FontMgrGetFamilyName (mgr, i);

      if (name)
        {
          families = Fcons (font_intern_prop (name, strlen (name), true),
                            families);
          OH_Drawing_FontMgrDestroyFamilyName (name);
        }
    }
  OH_Drawing_FontMgrDestroy (mgr);

  if (NILP (families))
    return list1 (ohosfont_default_family ());
  return Fnreverse (families);
}

static void
ohosfont_free_entity (Lisp_Object font_entity)
{
  /* Font entities of this driver only contain Lisp slots, which the
     font machinery frees itself; native resources are owned by the
     font objects created by `open_font'.  */
  (void) font_entity;
}

static Lisp_Object
ohosfont_open (struct frame *f, Lisp_Object entity, int pixel_size)
{
  Lisp_Object obj;
  struct ohosfont_info *info;
  struct font *font;
  OH_Drawing_FontStyleStruct style;
  const char *family;

  (void) f;
  if (pixel_size <= 0)
    pixel_size = 12;

  obj = font_make_object (VECSIZE (struct ohosfont_info), entity, pixel_size);
  info = (struct ohosfont_info *) XFONT_OBJECT (obj);
  /* font_make_object only clears the slots up to FONT_OBJECT_MAX, so
     the driver specific fields must be initialized here.  */
  info->drawing_font = NULL;
  info->cjk_font = NULL;
  info->drawing_typeface = NULL;
  info->cjk_typeface = NULL;

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
  font->encoding_charset = -1;
  font->repertory_charset = -1;

  family = ohosfont_family_name (AREF (entity, FONT_FAMILY_INDEX));
  style = ohosfont_drawing_style (entity);

  info->drawing_typeface = ohosfont_match_typeface (family, style);
  if (!info->drawing_typeface)
    info->drawing_typeface = OH_Drawing_TypefaceCreateDefault ();
  info->drawing_font = OH_Drawing_FontCreate ();
  if (info->drawing_font && info->drawing_typeface)
    {
      OH_Drawing_FontSetTypeface (info->drawing_font, info->drawing_typeface);
      OH_Drawing_FontSetTextSize (info->drawing_font, OHOSFONT_TEXT_SIZE);
    }

  info->cjk_typeface = ohosfont_match_cjk_typeface (family, style);
  info->cjk_font = OH_Drawing_FontCreate ();
  if (info->cjk_font && info->cjk_typeface)
    {
      OH_Drawing_FontSetTypeface (info->cjk_font, info->cjk_typeface);
      OH_Drawing_FontSetTextSize (info->cjk_font, OHOSFONT_CJK_TEXT_SIZE);
    }

  font->props[FONT_NAME_INDEX] = Ffont_xlfd_name (obj, Qnil, Qt);
  return obj;
}

Lisp_Object
ohosfont_make_default_object (struct frame *f)
{
  Lisp_Object entity = font_make_entity ();
  ASET (entity, FONT_TYPE_INDEX, Qohosfont);
  ASET (entity, FONT_FOUNDRY_INDEX, Qohosfont);
  ASET (entity, FONT_FAMILY_INDEX, ohosfont_default_family ());
  ASET (entity, FONT_ADSTYLE_INDEX, Qnil);
  ASET (entity, FONT_REGISTRY_INDEX, Qiso10646_1);
  ASET (entity, FONT_SIZE_INDEX, make_fixnum (18));
  ASET (entity, FONT_AVGWIDTH_INDEX, make_fixnum (0));
  ASET (entity, FONT_SPACING_INDEX, make_fixnum (FONT_SPACING_MONO));
  return ohosfont_open (f, entity, 18);
}

struct font *
ohosfont_make_default (struct frame *f)
{
  Lisp_Object entity = font_make_entity ();
  ASET (entity, FONT_TYPE_INDEX, Qohosfont);
  ASET (entity, FONT_FOUNDRY_INDEX, Qohosfont);
  ASET (entity, FONT_FAMILY_INDEX, ohosfont_default_family ());
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
  if (info->drawing_typeface)
    OH_Drawing_TypefaceDestroy (info->drawing_typeface);
  if (info->cjk_typeface && info->cjk_typeface != info->drawing_typeface)
    OH_Drawing_TypefaceDestroy (info->cjk_typeface);
  info->drawing_font = NULL;
  info->cjk_font = NULL;
  info->drawing_typeface = NULL;
  info->cjk_typeface = NULL;
}

static void
ohosfont_prepare_face (struct frame *f, struct face *face)
{
  /* The native renderer draws every glyph through OH_Drawing with the
     font's own typeface, so no per-face resources are needed.  */
  (void) f;
  (void) face;
}

static void
ohosfont_done_face (struct frame *f, struct face *face)
{
  (void) f;
  (void) face;
}

/* Return 1 if the font described by ENTITY can display C.  Font
   entities do not carry native fonts yet, so resolve the typefaces
   the same way `open_font' would.  This is only called when the
   display engine searches for a fallback font.  */

static int
ohosfont_entity_has_char (Lisp_Object entity, int c)
{
  const char *family = ohosfont_family_name (AREF (entity, FONT_FAMILY_INDEX));
  OH_Drawing_FontStyleStruct style = ohosfont_drawing_style (entity);
  OH_Drawing_Typeface *typefaces[2];
  OH_Drawing_Font *font;
  int i, result = 0;

  if (c < 0 || c > 0x10ffff)
    return 0;

  typefaces[0] = ohosfont_match_typeface (family, style);
  if (!typefaces[0])
    typefaces[0] = OH_Drawing_TypefaceCreateDefault ();
  typefaces[1] = ohosfont_match_cjk_typeface (family, style);

  for (i = 0; i < 2 && !result; i++)
    {
      if (!typefaces[i])
        continue;
      font = OH_Drawing_FontCreate ();
      if (!font)
        continue;
      OH_Drawing_FontSetTypeface (font, typefaces[i]);
      OH_Drawing_FontSetTextSize (font, (i == 0 ? OHOSFONT_TEXT_SIZE
                                               : OHOSFONT_CJK_TEXT_SIZE));
      result = ohosfont_glyph_code (font, (unsigned) c) != 0;
      OH_Drawing_FontDestroy (font);
    }

  if (typefaces[0] && typefaces[1] == typefaces[0])
    typefaces[1] = NULL;
  if (typefaces[0])
    OH_Drawing_TypefaceDestroy (typefaces[0]);
  if (typefaces[1])
    OH_Drawing_TypefaceDestroy (typefaces[1]);
  return result;
}

static int
ohosfont_has_char (Lisp_Object font, int c)
{
  struct ohosfont_info *info;

  if (FONT_ENTITY_P (font))
    return ohosfont_entity_has_char (font, c);
  if (!FONT_OBJECT_P (font))
    return 0;

  info = (struct ohosfont_info *) XFONT_OBJECT (font);
  if (c < 0 || c > 0x10ffff)
    return 0;
  return ohosfont_char_glyph (info, (unsigned) c) != 0;
}

static unsigned
ohosfont_encode_char (struct font *font, int c)
{
  struct ohosfont_info *info = (struct ohosfont_info *) font;

  /* Like the Android fallback driver, this backend uses the character
     code itself as the glyph code: the native renderer draws from the
     character, and `text_extents' measures characters.  Codes must fit
     in the two bytes the display engine keeps for them.  */
  if (c < 0 || c > 0xffff)
    return FONT_INVALID_CODE;
  if (ohosfont_char_glyph (info, (unsigned) c) == 0)
    return FONT_INVALID_CODE;
  return (unsigned) c;
}

static void
ohosfont_text_extents (struct font *font, const unsigned *code, int nglyphs,
                       struct font_metrics *metrics)
{
  struct ohosfont_info *info = (struct ohosfont_info *) font;
  int width = 0;
  int cell = font->space_width > 0 ? font->space_width : 9;
  int i;

  for (i = 0; i < nglyphs; i++)
    {
      int advance = ohosfont_char_width (info, code[i]);

      if (advance < 0)
        advance = ohosfont_wide (code[i]) ? 2 * cell : cell;
      width += advance;
    }
  metrics->lbearing = 0;
  metrics->rbearing = width;
  metrics->width = width;
  metrics->ascent = font->ascent;
  metrics->descent = font->descent;
}

/* Return the flags passed to the native renderer for the glyph
   string S.  */

static uint32_t
ohosfont_face_flags (struct glyph_string *s, bool with_background)
{
  uint32_t flags = 0;

  if (with_background && s->face->background != s->face->foreground)
    flags |= 1;
  if (s->face->underline != FACE_NO_UNDERLINE)
    flags |= 4;
  if (s->font && ohosfont_font_weight (s->font) >= 150)
    flags |= 2;
  if (s->face->strike_through_p)
    flags |= 8;
  if (s->face->overline_p)
    flags |= 16;
  return flags;
}

/* Colours for a glyph string.

   Draw_cursor means reverse video: every backend swaps the face colours
   for the glyph under the cursor, and our driver has to do the same or
   the cursor is drawn exactly like plain text (invisible).  */

static inline uint32_t
ohosfont_fg (struct glyph_string *s)
{
  return (uint32_t) (s->hl == DRAW_CURSOR
                     ? s->face->background : s->face->foreground);
}

static inline uint32_t
ohosfont_bg (struct glyph_string *s)
{
  return (uint32_t) (s->hl == DRAW_CURSOR
                     ? s->face->foreground : s->face->background);
}

/* Draw the glyphs of an automatic composition.  The characters to
   draw are recorded in the composition's glyph string rather than in
   the row glyphs, which only carry glyph codes.  */

static int
ohosfont_draw_gstring (struct glyph_string *s, int x, int y,
                       bool with_background)
{
  Lisp_Object gstring;
  uint32_t flags;
  int line_height;
  ptrdiff_t i;

  gstring = composition_gstring_from_id (s->cmp_id);
  if (!composition_gstring_p (gstring))
    return 0;

  line_height = FRAME_LINE_HEIGHT (s->f);
  flags = ohosfont_face_flags (s, with_background);
  for (i = s->cmp_from; i < s->cmp_to; i++)
    {
      Lisp_Object lglyph = LGSTRING_GLYPH (gstring, i);
      int code, width;

      if (NILP (lglyph))
        continue;
      code = LGLYPH_CHAR (lglyph);
      if (code < 0)
        continue;
      width = LGLYPH_WIDTH (lglyph) + LGLYPH_WADJUST (lglyph);
      if (width < 0)
        width = 0;
      x += LGLYPH_XOFF (lglyph);
      ohos_draw_text (x, y, width, line_height, (unsigned) code,
                      ohosfont_fg (s), ohosfont_bg (s), flags);
      x += width;
    }
  return (int) (s->cmp_to - s->cmp_from);
}

static int
ohosfont_draw (struct glyph_string *s, int from, int to, int x, int y,
               bool with_background)
{
  int line_height;
  uint32_t flags;
  int i;

  if (!s || !s->face || !s->first_glyph)
    return 0;

  /* Automatic compositions keep their characters in the glyph
     string; their glyph strings have no character glyphs, so the
     range passed by the redisplay interface is empty.  Draw the whole
     composition in that case.  */
  if (s->first_glyph->type == COMPOSITE_GLYPH)
    {
      if (s->first_glyph->u.cmp.automatic)
        return ohosfont_draw_gstring (s, x, y, with_background);
      /* Static compositions are not supported by the native renderer
         yet; they are left to the generic glyphless display.  */
      return 0;
    }
  if (from >= to || s->first_glyph->type != CHAR_GLYPH)
    return 0;

  line_height = FRAME_LINE_HEIGHT (s->f);
  flags = ohosfont_face_flags (s, with_background);
  for (i = from; i < to; i++)
    {
      struct glyph *g = s->first_glyph + i;
      int w = g->pixel_width > 0 ? g->pixel_width : FRAME_COLUMN_WIDTH (s->f);

      if (w <= 0)
        continue;
      ohos_draw_text (x, y, w, line_height, g->u.ch,
                      ohosfont_fg (s), ohosfont_bg (s), flags);
      x += w;
    }
  return to - from;
}

static int
ohosfont_anchor_point (struct font *font, unsigned code, int index,
                       int *x, int *y)
{
  int width;

  /* OH_Drawing does not expose glyph outlines at this API level, so
     the only anchor point available is the centre of the advance box
     on the baseline.  */
  if (index != 0 || !x || !y)
    return -1;
  width = ohosfont_char_width ((struct ohosfont_info *) font, code);
  if (width < 0)
    return -1;
  *x = width / 2;
  *y = 0;
  return 0;
}

static Lisp_Object
ohosfont_otf_capability (struct font *font)
{
  /* OH_Drawing does not expose the GSUB/GPOS tables of a typeface at
     this API level, so report an empty capability list, as the font
     machinery does for drivers without OTF support.  */
  (void) font;
  return Fcons (Qnil, Qnil);
}

static int
ohosfont_start_for_frame (struct frame *f)
{
  /* All native resources belong to font objects; nothing has to be
     prepared for a frame.  */
  (void) f;
  return 0;
}

static int
ohosfont_end_for_frame (struct frame *f)
{
  (void) f;
  return 0;
}

/* Shape the glyph string LGSTRING with the OH_Drawing glyph API.

   OH_Drawing converts text to glyphs one character at a time and does
   not expose glyph positioning at this API level, so the result is a
   one-to-one mapping between input characters and output glyphs whose
   advances come from the platform.  This is enough to fill the
   FROM/TO/CHAR/CODE/WIDTH fields required by the glyph string
   contract, and it lets the display engine use the real advances
   instead of the fixed cell grid.  */

static Lisp_Object
ohosfont_shape (Lisp_Object lgstring, Lisp_Object direction)
{
  struct font *font = CHECK_FONT_GET_OBJECT (LGSTRING_FONT (lgstring));
  struct ohosfont_info *info = (struct ohosfont_info *) font;
  ptrdiff_t capacity = LGSTRING_GLYPH_LEN (lgstring);
  ptrdiff_t text_len = 0, byte_len = 0, glyph_count, i;
  unsigned *chars;
  uint16_t *glyphs;
  float *widths;
  char *utf8;
  OH_Drawing_Font *drawing_font;
  Lisp_Object result = Qnil;
  int cell = font->space_width > 0 ? font->space_width : 9;
  USE_SAFE_ALLOCA;

  (void) direction;

  /* The input glyphs end at the first nil element.  */
  while (text_len < capacity && !NILP (LGSTRING_GLYPH (lgstring, text_len)))
    text_len++;
  if (text_len == 0)
    return Qnil;

  SAFE_NALLOCA (chars, 1, text_len);
  SAFE_NALLOCA (glyphs, 1, text_len);
  SAFE_NALLOCA (widths, 1, text_len);
  SAFE_NALLOCA (utf8, 4, text_len + 1);

  for (i = 0; i < text_len; i++)
    {
      Lisp_Object lglyph = LGSTRING_GLYPH (lgstring, i);
      int c = LGLYPH_CHAR (lglyph);

      chars[i] = (unsigned) c;
      byte_len += ohosfont_utf8 ((unsigned) c, utf8 + byte_len);
      glyphs[i] = 0;
      widths[i] = 0.0f;
    }
  utf8[byte_len] = '\0';

  drawing_font = ohosfont_pick_font (info, chars[0]);
  if (!drawing_font)
    goto done;

  glyph_count = OH_Drawing_FontTextToGlyphs (drawing_font, utf8, byte_len,
                                             TEXT_ENCODING_UTF8, glyphs,
                                             (int) text_len);
  if (glyph_count == text_len)
    OH_Drawing_FontGetWidths (drawing_font, glyphs, (int) text_len, widths);
  else
    {
      /* The text did not map one glyph per character (for instance
         because some characters are missing from the selected font).
         Shape each character on its own so that FROM/TO still cover
         the whole input.  */
      for (i = 0; i < text_len; i++)
        {
          int width = ohosfont_char_width (info, chars[i]);

          glyphs[i] = (uint16_t) ohosfont_char_glyph (info, chars[i]);
          widths[i] = (width < 0 ? 0.0f : (float) width);
        }
      glyph_count = text_len;
    }

  for (i = 0; i < glyph_count; i++)
    {
      Lisp_Object lglyph = LGSTRING_GLYPH (lgstring, i);
      int width, code;

      if (NILP (lglyph))
        {
          lglyph = LGLYPH_NEW ();
          LGSTRING_SET_GLYPH (lgstring, i, lglyph);
          LGLYPH_SET_FROM (lglyph, i);
          LGLYPH_SET_TO (lglyph, i);
          LGLYPH_SET_CHAR (lglyph, chars[i]);
        }

      width = (int) (widths[i] + 0.5f);
      if (glyphs[i] == 0)
        width = ohosfont_wide (chars[i]) ? 2 * cell : cell;
      else if (width <= 0 && !ohosfont_combining_p (chars[i]))
        width = ohosfont_wide (chars[i]) ? 2 * cell : cell;

      /* encode_char uses character codes as glyph codes.  */
      code = (chars[i] <= 0xffff ? (int) chars[i] : (int) glyphs[i]);
      LGLYPH_SET_CODE (lglyph, code);
      LGLYPH_SET_WIDTH (lglyph, width);
      LGLYPH_SET_LBEARING (lglyph, 0);
      LGLYPH_SET_RBEARING (lglyph, width);
      LGLYPH_SET_ASCENT (lglyph, font->ascent);
      LGLYPH_SET_DESCENT (lglyph, font->descent);
    }

  result = make_fixnum (glyph_count);

 done:
  SAFE_FREE ();
  return result;
}

static int
ohosfont_check (struct frame *f, struct font *font)
{
  struct ohosfont_info *info = (struct ohosfont_info *) font;

  (void) f;
  return (info->drawing_font || info->cjk_font) ? 0 : -1;
}

/* Fill VARIATIONS with the glyphs the font uses for the Unicode
   variation selectors of C.  The OH_Drawing text API does not expose
   the format 14 cmap subtable, so a selector is only reported when
   converting C followed by the selector produces a different glyph.
   The selector's own glyph must have a zero advance for the mapping
   to be trusted.  */

static int
ohosfont_get_variation_glyphs (struct font *font, int c,
                               unsigned variations[256])
{
  struct ohosfont_info *info = (struct ohosfont_info *) font;
  OH_Drawing_Font *drawing_font;
  unsigned base_glyph;
  char buf[8];
  int base_len, i, count = 0;

  if (c < 0 || c > 0x10ffff)
    return 0;
  memset (variations, 0, sizeof *variations * 256);

  drawing_font = ohosfont_pick_font (info, (unsigned) c);
  base_glyph = ohosfont_glyph_code (drawing_font, (unsigned) c);
  if (base_glyph == 0)
    return 0;

  base_len = ohosfont_utf8 ((unsigned) c, buf);
  for (i = 0; i < 256; i++)
    {
      uint16_t glyphs[2] = { 0, 0 };
      unsigned selector = (i < 16 ? 0xfe00 + i : 0xe0100 + (i - 16));
      int len, n;

      len = base_len + ohosfont_utf8 (selector, buf + base_len);
      n = OH_Drawing_FontTextToGlyphs (drawing_font, buf, len,
                                       TEXT_ENCODING_UTF8, glyphs, 2);
      if (n == 2 && glyphs[1] != 0)
        {
          float advance = -1.0f;

          OH_Drawing_FontGetWidths (drawing_font, &glyphs[1], 1, &advance);
          if (advance == 0.0f)
            {
              variations[i] = glyphs[1];
              count++;
            }
        }
      else if (n == 1 && glyphs[0] != 0 && glyphs[0] != base_glyph)
        {
          /* The platform substituted the base glyph itself.  */
          variations[i] = glyphs[0];
          count++;
        }
    }
  return count;
}

/* Properties accepted by this driver.  */

static const char *const ohosfont_booleans[] =
  {
    ":antialias",
    ":scalable",
    ":outline",
    NULL,
  };

static const char *const ohosfont_non_booleans[] =
  {
    ":family",
    ":foundry",
    ":style",
    ":slant",
    ":weight",
    ":width",
    ":size",
    ":pixelsize",
    ":spacing",
    ":dpi",
    NULL,
  };

static void
ohosfont_filter_properties (Lisp_Object font, Lisp_Object properties)
{
  font_filter_properties (font, properties, ohosfont_booleans,
                          ohosfont_non_booleans);
}

/* Return true if the cached FONT-OBJECT of ENTITY can still be used.
   The native typeface depends on the family and style recorded in the
   entity, so a cached font is only valid for the same values.  */

static bool
ohosfont_cached_font_ok (struct frame *f, Lisp_Object font_object,
                         Lisp_Object entity)
{
  struct font *font = XFONT_OBJECT (font_object);
  int i;

  (void) f;
  if (font->driver != &ohosfont_driver)
    return false;
  for (i = FONT_FOUNDRY_INDEX; i <= FONT_WIDTH_INDEX; i++)
    if (!EQ (AREF (font_object, i), AREF (entity, i)))
      return false;
  return true;
}

static Lisp_Object
ohosfont_combining_capability (struct font *font)
{
  /* The driver shapes combining sequences but cannot position their
     glyphs, since OH_Drawing does not expose glyph offsets at this
     API level.  Report nil so that callers such as hebrew.el use
     their own fallback alignment instead of leaving the marks at the
     pen position.  */
  (void) font;
  return Qnil;
}

static struct font_driver ohosfont_driver =
  {
    .type = 0,
    .case_sensitive = false,
    .get_cache = ohosfont_get_cache,
    .list = ohosfont_list,
    .match = ohosfont_match,
    .list_family = ohosfont_list_family,
    .free_entity = ohosfont_free_entity,
    .open_font = ohosfont_open,
    .close_font = ohosfont_close,
    .prepare_face = ohosfont_prepare_face,
    .done_face = ohosfont_done_face,
    .has_char = ohosfont_has_char,
    .encode_char = ohosfont_encode_char,
    .text_extents = ohosfont_text_extents,
    .draw = ohosfont_draw,
    .anchor_point = ohosfont_anchor_point,
    .otf_capability = ohosfont_otf_capability,
    .start_for_frame = ohosfont_start_for_frame,
    .end_for_frame = ohosfont_end_for_frame,
    .shape = ohosfont_shape,
    .check = ohosfont_check,
    .get_variation_glyphs = ohosfont_get_variation_glyphs,
    .filter_properties = ohosfont_filter_properties,
    .cached_font_ok = ohosfont_cached_font_ok,
    .combining_capability = ohosfont_combining_capability,
  };

void
ohosfont_register_frame (struct frame *f)
{
  register_font_driver (&ohosfont_driver, f);
}

void
syms_of_ohosfont (void)
{
  DEFSYM (Qohosfont, "ohos");
  ohosfont_driver.type = Qohosfont;
  ohosfont_cache = list (Qnil);
  staticpro (&ohosfont_cache);
  register_font_driver (&ohosfont_driver, NULL);
}
