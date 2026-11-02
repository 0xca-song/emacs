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

struct ohosfont_info
{
  struct font font;
};

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
  (void) font;
}

static int
ohosfont_has_char (Lisp_Object font, int c)
{
  return c >= 0 && c <= 0x10ffff;
}

static unsigned
ohosfont_encode_char (struct font *font, int c)
{
  (void) font;
  return (unsigned) c;
}

static void
ohosfont_text_extents (struct font *font, const unsigned *code, int nglyphs,
                       struct font_metrics *metrics)
{
  int width = 0;
  for (int i = 0; i < nglyphs; i++)
    width += ohosfont_wide (code[i]) ? 18 : 9;
  metrics->lbearing = 0;
  metrics->rbearing = width;
  metrics->width = width;
  metrics->ascent = ohos_ascent;
  metrics->descent = ohos_descent;
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
