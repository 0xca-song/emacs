/* Image decoding for the OHOS GUI backend.

   PNG is handled by libpng through Emacs' own `png_load' (HAVE_PNG), so
   this file only provides the SVG type: Emacs' svg loader needs librsvg,
   which cannot be built here (GLib stack + meson), so the port parses and
   rasterises SVG itself with the vendored nanosvg headers (zlib licence,
   no dependencies).

   Both paths end the same way: the decoded pixels are stored as an RGBA
   string in `img->lisp_data', and the backend blits it when redisplay
   hands it an IMAGE_GLYPH.  */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "lisp.h"
#include "frame.h"
#include "window.h"
#include "dispextern.h"
#include "ohosgui.h"

/* The backend keeps decoded pixels in `img->pixmap', the opaque handle
   image.c reserves for the display backend: it is not touched when Emacs
   copies or scales an image, unlike `lisp_data' which image_clear_image
   resets.  */
void
ohos_image_free (struct frame *f, struct image *img)
{
  struct ohos_image_bitmap *bmp = img->pixmap;

  (void) f;
  if (bmp)
    {
      xfree (bmp->rgba);
      xfree (bmp);
      img->pixmap = NULL;
    }
  img->lisp_data = Qnil;
}

static struct ohos_image_bitmap *
ohos_bitmap_new (int w, int h)
{
  struct ohos_image_bitmap *bmp = xmalloc (sizeof *bmp);

  bmp->width = w;
  bmp->height = h;
  bmp->rgba = xmalloc ((size_t) w * h * 4);
  memset (bmp->rgba, 0, (size_t) w * h * 4);
  return bmp;
}

#include <png.h>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

static Lisp_Object Qohos_image_file;
static Lisp_Object Qohos_image_data;

static unsigned char *
ohos_read_whole_file (const char *name, size_t *len)
{
  FILE *fp = fopen (name, "rb");
  unsigned char *buf = NULL;
  size_t cap = 0, used = 0;

  if (!fp)
    return NULL;
  for (;;)
    {
      size_t n;

      /* Keep room for the terminating NUL: the SVG parser expects a C
	 string.  */
      if (used + 65536 + 1 > cap)
	{
	  cap = cap ? cap * 2 : 131072;
	  if (cap < used + 65536 + 1)
	    cap = used + 65536 + 1;
	  buf = xrealloc (buf, cap);
	}
      n = fread (buf + used, 1, cap - used, fp);
      used += n;
      if (n == 0)
	break;
    }
  fclose (fp);
  buf[used] = '\0';
  *len = used;
  return buf;
}

/* An image spec is `(image :type T :file F ...)`: the leading symbol has to
   be dropped before the rest can be read as a property list.  */

static Lisp_Object
ohos_spec_plist (Lisp_Object spec)
{
  return (CONSP (spec) && EQ (XCAR (spec), Qimage)) ? XCDR (spec) : spec;
}

bool
ohos_png_image_p (Lisp_Object spec)
{
  Lisp_Object plist = ohos_spec_plist (spec);

  return (STRINGP (Fplist_get (plist, Qohos_image_file, Qnil))
	  || STRINGP (Fplist_get (plist, Qohos_image_data, Qnil)));
}

bool
ohos_png_load (struct frame *f, struct image *img)
{
  Lisp_Object plist = ohos_spec_plist (img->spec);
  Lisp_Object file = Fplist_get (plist, Qohos_image_file, Qnil);
  Lisp_Object data = Fplist_get (plist, Qohos_image_data, Qnil);
  char *owned = NULL;
  const unsigned char *bytes;
  size_t len;
  png_image png;
  struct ohos_image_bitmap *bmp;

  (void) f;
  if (STRINGP (file))
    {
      owned = (char *) ohos_read_whole_file (SSDATA (file), &len);
      if (!owned)
	return false;
      bytes = (unsigned char *) owned;
    }
  else if (STRINGP (data))
    {
      bytes = SDATA (data);
      len = SBYTES (data);
    }
  else
    return false;

  memset (&png, 0, sizeof png);
  png.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory (&png, bytes, len))
    {
      xfree (owned);
      ohos_log ("png load: begin_read failed");
      return false;
    }
  png.format = PNG_FORMAT_RGBA;
  bmp = ohos_bitmap_new ((int) png.width, (int) png.height);
  if (!png_image_finish_read (&png, NULL, bmp->rgba, 0, NULL))
    {
      xfree (owned);
      xfree (bmp->rgba);
      xfree (bmp);
      png_image_free (&png);
      return false;
    }
  png_image_free (&png);
  xfree (owned);

  img->pixmap = bmp;
  img->width = bmp->width;
  img->height = bmp->height;
  {
    char d[80];
    snprintf (d, sizeof d, "png load ok img=%p %dx%d", (void *) img,
              bmp->width, bmp->height);
    ohos_log (d);
  }
  return true;
}

bool
ohos_svg_image_p (Lisp_Object spec)
{
  Lisp_Object plist = ohos_spec_plist (spec);
  bool ok = (STRINGP (Fplist_get (plist, Qohos_image_file, Qnil))
             || STRINGP (Fplist_get (plist, Qohos_image_data, Qnil)));

  return ok;
}

bool
ohos_svg_load (struct frame *f, struct image *img)
{
  Lisp_Object plist = ohos_spec_plist (img->spec);
  Lisp_Object file = Fplist_get (plist, Qohos_image_file, Qnil);
  Lisp_Object data = Fplist_get (plist, Qohos_image_data, Qnil);
  char *owned = NULL;
  const char *text = NULL;
  NSVGimage *svg;
  NSVGrasterizer *rast;
  unsigned char *rgba;
  int w, h;
  float scale;

  (void) f;
  if (STRINGP (file))
    {
      size_t len;

      owned = (char *) ohos_read_whole_file (SSDATA (file), &len);
      if (!owned)
        return false;
      text = owned;
    }
  else if (STRINGP (data))
    text = SSDATA (data);
  else
    return false;

  svg = nsvgParse ((char *) text, "px", 96.0f);
  xfree (owned);
  if (!svg)
    {
      ohos_log ("svg: parse failed");
      return false;
    }
  if (svg->width <= 0 || svg->height <= 0)
    {
      nsvgDelete (svg);
      return false;
    }

  /* Honour :width/:height when Emacs asked for a size; otherwise use the
     document's own size.  */
  w = img->width > 0 ? img->width : (int) (svg->width + 0.5f);
  h = img->height > 0 ? img->height : (int) (svg->height + 0.5f);
  if (w <= 0 || h <= 0)
    {
      nsvgDelete (svg);
      return false;
    }
  scale = (float) w / svg->width;
  if ((float) h / svg->height < scale)
    scale = (float) h / svg->height;

  rgba = xmalloc ((size_t) w * h * 4);
  memset (rgba, 0, (size_t) w * h * 4);
  rast = nsvgCreateRasterizer ();
  if (!rast)
    {
      xfree (rgba);
      nsvgDelete (svg);
      return false;
    }
  nsvgRasterize (rast, svg, 0, 0, scale, rgba, w, h, w * 4);
  nsvgDeleteRasterizer (rast);
  nsvgDelete (svg);

  ohos_log ("svg load ok");
  img->pixmap = xmalloc (sizeof (struct ohos_image_bitmap));
  ((struct ohos_image_bitmap *) img->pixmap)->width = w;
  ((struct ohos_image_bitmap *) img->pixmap)->height = h;
  ((struct ohos_image_bitmap *) img->pixmap)->rgba = rgba;
  img->width = w;
  img->height = h;
  return true;
}

void
syms_of_ohosimage (void)
{
  Qohos_image_file = intern_c_string (":file");
  staticpro (&Qohos_image_file);
  Qohos_image_data = intern_c_string (":data");
  staticpro (&Qohos_image_data);
}
