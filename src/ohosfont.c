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

static Lisp_Object Qohosfont;
static struct font_driver ohosfont_driver;

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
  font->height = 18;
  font->ascent = 14;
  font->descent = 4;
  font->space_width = 9;
  font->min_width = 9;
  font->max_width = 9;
  font->average_width = 9;
  font->underline_position = 15;
  font->underline_thickness = 1;
  font->default_ascent = 14;
  font->vertical_centering = 0;
  font->baseline_offset = 0;
  font->relative_compose = 0;
  font->props[FONT_NAME_INDEX] = build_string ("Noto Sans Mono-12");
  font->props[FONT_FAMILY_INDEX] = intern ("Noto Sans Mono");
  font->props[FONT_REGISTRY_INDEX] = Qiso10646_1;
  return obj;
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
    width += (code[i] >= 0x1100 && code[i] <= 0x115f)
      || (code[i] >= 0x2e80 && code[i] <= 0xa4cf) ? 18 : 9;
  metrics->lbearing = 0;
  metrics->rbearing = width;
  metrics->width = width;
  metrics->ascent = 14;
  metrics->descent = 4;
}

static int
ohosfont_draw (struct glyph_string *s, int from, int to, int x, int y,
               bool with_background)
{
  if (!s || from >= to)
    return 0;
  for (int i = from; i < to; i++)
    {
      struct glyph *g = s->first_glyph + i;
      if (g->type != CHAR_GLYPH)
        continue;
      int w = (g->u.ch >= 0x1100 && g->u.ch <= 0x115f)
        || (g->u.ch >= 0x2e80 && g->u.ch <= 0xa4cf) ? 18 : 9;
      ohos_frame_text (x, y, w, g->u.ch,
                       (uint32_t) s->face->foreground,
                       (uint32_t) s->face->background,
                       with_background);
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
