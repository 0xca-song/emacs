/* Image support for the OHOS GUI backend.

   Pixels live in a backend pixmap that is also the x_image (see
   ohosgui.h), which is exactly the model cairo and haiku use: image.c
   creates it, decoders write into it through PUT_PIXEL, and the backend
   only has to blit it.  PNG therefore goes through Emacs' own png_load
   (HAVE_PNG); this file provides the pixmap primitives and the SVG type,
   which Emacs can only decode through librsvg (unavailable here), so the
   port decodes it with the vendored nanosvg and stores the result with
   the very same primitives.

   This branch is a local experiment and is not intended for upstream.  */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "lisp.h"
#include "frame.h"
#include "window.h"
#include "dispextern.h"
#include "ohosgui.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

struct ohos_pixmap *
ohos_create_pixmap (int width, int height, int depth)
{
  struct ohos_pixmap *p;

  if (width <= 0 || height <= 0)
    return NULL;
  if (depth == 0)
    depth = 32;
  if (depth != 32 && depth != 1)
    return NULL;

  p = xmalloc (sizeof *p);
  memset (p, 0, sizeof *p);
  p->width = width;
  p->height = height;
  p->depth = depth;
  p->bits_per_pixel = depth;
  p->bytes_per_line = depth == 32 ? width * 4 : (width + 7) / 8;
  p->data = xmalloc ((size_t) p->bytes_per_line * height);
  memset (p->data, 0, (size_t) p->bytes_per_line * height);
  return p;
}

void
ohos_free_image_pixmap (struct frame *f, void *pixmap)
{
  struct ohos_pixmap *p = pixmap;

  (void) f;
  if (p)
    {
      xfree (p->data);
      xfree (p);
    }
}

void
ohos_put_pixel (struct ohos_pixmap *p, int x, int y, unsigned long color)
{
  if (!p || x < 0 || y < 0 || x >= p->width || y >= p->height)
    return;
  if (p->depth == 32)
    ((unsigned int *) (p->data + (size_t) y * p->bytes_per_line))[x]
      = (unsigned int) color;
  else
    {
      char *byte = p->data + (size_t) y * p->bytes_per_line + x / 8;

      if (color)
	*byte |= 1 << (x % 8);
      else
	*byte &= ~(1 << (x % 8));
    }
}

unsigned long
ohos_get_pixel (struct ohos_pixmap *p, int x, int y)
{
  if (!p || x < 0 || y < 0 || x >= p->width || y >= p->height)
    return 0;
  if (p->depth == 32)
    return ((unsigned int *) (p->data + (size_t) y * p->bytes_per_line))[x];
  return ((unsigned char *) p->data)[(size_t) y * p->bytes_per_line + x / 8]
	 >> (x % 8) & 1;
}

/* ---- SVG ---------------------------------------------------------- */

static Lisp_Object Qohos_image_file;
static Lisp_Object Qohos_image_data;

/* The image spec may name the file relative to `image-load-path' (the
   startup screen passes "splash.svg"), so resolve it exactly the way
   png_load does before opening it.  */
extern Lisp_Object image_find_image_fd (Lisp_Object, int *);

static unsigned char *
ohos_read_whole_file (const char *name, size_t *len)
{
  int fd = -1;
  Lisp_Object found = image_find_image_fd (build_string (name), &fd);
  FILE *fp = NULL;

  if (STRINGP (found))
    fp = fopen (SSDATA (found), "rb");
  else if (fd >= 0)
    fp = fdopen (fd, "rb");
  if (!fp)
    fp = fopen (name, "rb");

  unsigned char *buf = NULL;
  size_t cap = 0, used = 0;

  if (!fp)
    return NULL;
  for (;;)
    {
      size_t n;

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

static Lisp_Object
ohos_spec_plist (Lisp_Object spec)
{
  return (CONSP (spec) && EQ (XCAR (spec), Qimage)) ? XCDR (spec) : spec;
}

bool
ohos_svg_image_p (Lisp_Object spec)
{
  Lisp_Object plist = ohos_spec_plist (spec);

  return (STRINGP (Fplist_get (plist, Qohos_image_file, Qnil))
	  || STRINGP (Fplist_get (plist, Qohos_image_data, Qnil)));
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
  struct ohos_pixmap *pix;
  int w, h, x, y;
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
  if (!svg || svg->width <= 0 || svg->height <= 0)
    {
      if (svg)
	nsvgDelete (svg);
      return false;
    }

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

  pix = ohos_create_pixmap (w, h, 32);
  if (!pix)
    {
      xfree (rgba);
      return false;
    }
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
	unsigned char *p = rgba + ((size_t) y * w + x) * 4;
	unsigned long color
	  = ((unsigned long) p[3] << 24) | ((unsigned long) p[0] << 16)
	    | ((unsigned long) p[1] << 8) | p[2];

	ohos_put_pixel (pix, x, y, color);
      }
  xfree (rgba);

  img->pixmap = pix;
  img->width = w;
  img->height = h;
  return true;
}

/* free_img for the SVG type: the pixmap is released through the terminal's
   free_pixmap hook, which is ohos_free_image_pixmap.  */
void
ohos_image_free (struct frame *f, struct image *img)
{
  img->lisp_data = Qnil;
  if (img->pixmap)
    {
      ohos_free_image_pixmap (f, img->pixmap);
      img->pixmap = NULL;
    }
}

void
syms_of_ohosimage (void)
{
  Qohos_image_file = intern_c_string (":file");
  staticpro (&Qohos_image_file);
  Qohos_image_data = intern_c_string (":data");
  staticpro (&Qohos_image_data);
}
