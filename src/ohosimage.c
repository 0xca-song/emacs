/* PNG images for the OHOS GUI backend.

   No image library is enabled in this build (HAVE_PNG and friends are
   undefined) but zlib is available, so PNG is decoded here and the pixels
   are handed to redisplay as an RGBA string in `img->lisp_data'.  The
   backend blits that string through the draw protocol (IMAGE_GLYPH).

   Supported: 8 and 16 bit samples, colour types 0/2/3/4/6, no
   interlacing.  Larger PNGs work; interlaced ones are rejected.  */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>

#include "lisp.h"
#include "frame.h"
#include "window.h"
#include "dispextern.h"
#include "ohosgui.h"

static Lisp_Object Qohos_png_file;
static Lisp_Object Qohos_png_data;

static unsigned
be32 (const unsigned char *p)
{
  return ((unsigned) p[0] << 24) | ((unsigned) p[1] << 16)
    | ((unsigned) p[2] << 8) | (unsigned) p[3];
}

static unsigned char *
read_whole_file (const char *name, size_t *len)
{
  FILE *fp = fopen (name, "rb");
  unsigned char *buf = NULL;
  size_t cap = 0, used = 0;

  if (!fp)
    return NULL;
  for (;;)
    {
      size_t n;

      if (used + 65536 > cap)
	{
	  cap = cap ? cap * 2 : 131072;
	  buf = xrealloc (buf, cap);
	}
      n = fread (buf + used, 1, cap - used, fp);
      used += n;
      if (n == 0)
	break;
    }
  fclose (fp);
  *len = used;
  return buf;
}

/* Decode PNG DATA into a freshly allocated RGBA buffer.  */

static bool
png_decode (const unsigned char *data, size_t len, int *out_w, int *out_h,
	    unsigned char **out_rgba)
{
  static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
  const unsigned char *p, *end;
  int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
  unsigned char palette[256][4];
  int npal = 0;
  unsigned char *idat = NULL;
  size_t idat_len = 0, idat_cap = 0;
  unsigned char *raw = NULL, *rows = NULL, *rgba = NULL;
  size_t raw_size, stride;
  int channels, bpp, x, y;
  int ok = false;

  if (len < 8 || memcmp (data, sig, 8) != 0)
    return false;
  memset (palette, 255, sizeof palette);
  p = data + 8;
  end = data + len;
  while (p + 8 <= end)
    {
      unsigned clen = be32 (p);
      const unsigned char *type = p + 4;
      const unsigned char *chunk = p + 8;

      if (clen > (unsigned) (end - chunk) || p + 12 + clen > end)
	break;
      if (!memcmp (type, "IHDR", 4) && clen >= 13)
	{
	  w = (int) be32 (chunk);
	  h = (int) be32 (chunk + 4);
	  depth = chunk[8];
	  ctype = chunk[9];
	  interlace = chunk[12];
	}
      else if (!memcmp (type, "PLTE", 4))
	{
	  npal = (int) (clen / 3);
	  if (npal > 256)
	    npal = 256;
	  for (int i = 0; i < npal; i++)
	    {
	      palette[i][0] = chunk[i * 3];
	      palette[i][1] = chunk[i * 3 + 1];
	      palette[i][2] = chunk[i * 3 + 2];
	      palette[i][3] = 255;
	    }
	}
      else if (!memcmp (type, "tRNS", 4))
	{
	  for (unsigned i = 0; i < clen && i < 256; i++)
	    palette[i][3] = chunk[i];
	}
      else if (!memcmp (type, "IDAT", 4))
	{
	  if (idat_len + clen > idat_cap)
	    {
	      idat_cap = (idat_len + clen) * 2 + 1024;
	      idat = xrealloc (idat, idat_cap);
	    }
	  memcpy (idat + idat_len, chunk, clen);
	  idat_len += clen;
	}
      else if (!memcmp (type, "IEND", 4))
	break;
      p += 12 + clen;
    }

  if (w <= 0 || h <= 0 || interlace != 0 || !idat
      || (depth != 8 && depth != 16))
    goto done;
  switch (ctype)
    {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: goto done;
    }
  if (ctype == 3 && npal == 0)
    goto done;

  bpp = channels * (depth == 16 ? 2 : 1);
  stride = (size_t) w * bpp;
  raw_size = (stride + 1) * (size_t) h;
  raw = xmalloc (raw_size);
  {
    uLongf out_len = (uLongf) raw_size;

    if (uncompress (raw, &out_len, idat, (uLong) idat_len) != Z_OK
	|| out_len < raw_size)
      goto done;
  }

  rows = xmalloc (stride * (size_t) h);
  for (y = 0; y < h; y++)
    {
      const unsigned char *src = raw + (size_t) y * (stride + 1) + 1;
      unsigned char *dst = rows + (size_t) y * stride;
      const unsigned char *prev = y ? rows + (size_t) (y - 1) * stride : NULL;
      int filter = raw[(size_t) y * (stride + 1)];
      size_t i;

      for (i = 0; i < stride; i++)
	{
	  int a = i >= (size_t) bpp ? dst[i - bpp] : 0;
	  int b = prev ? prev[i] : 0;
	  int c = (prev && i >= (size_t) bpp) ? prev[i - bpp] : 0;
	  int v = src[i];

	  switch (filter)
	    {
	    case 0: break;
	    case 1: v += a; break;
	    case 2: v += b; break;
	    case 3: v += (a + b) / 2; break;
	    case 4:
	      {
		int pp = a + b - c;
		int pa = abs (pp - a), pb = abs (pp - b), pc = abs (pp - c);

		v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
	      }
	      break;
	    default:
	      goto done;
	    }
	  dst[i] = (unsigned char) v;
	}
    }

  rgba = xmalloc ((size_t) w * h * 4);
  for (y = 0; y < h; y++)
    for (x = 0; x < w; x++)
      {
	const unsigned char *px = rows + (size_t) y * stride + (size_t) x * bpp;
	unsigned char *out = rgba + ((size_t) y * w + x) * 4;

	switch (ctype)
	  {
	  case 0:
	    out[0] = out[1] = out[2] = px[0];
	    out[3] = 255;
	    break;
	  case 2:
	    out[0] = px[0]; out[1] = px[1]; out[2] = px[2]; out[3] = 255;
	    break;
	  case 3:
	    {
	      int idx = px[0];

	      if (idx >= npal)
		idx = 0;
	      out[0] = palette[idx][0];
	      out[1] = palette[idx][1];
	      out[2] = palette[idx][2];
	      out[3] = palette[idx][3];
	    }
	    break;
	  case 4:
	    out[0] = out[1] = out[2] = px[0];
	    out[3] = px[(depth == 16) ? 2 : 1];
	    break;
	  default:
	    out[0] = px[0]; out[1] = px[1]; out[2] = px[2];
	    out[3] = px[(depth == 16) ? 6 : 3];
	    break;
	  }
      }

  *out_w = w;
  *out_h = h;
  *out_rgba = rgba;
  rgba = NULL;
  ok = true;

 done:
  xfree (idat);
  xfree (raw);
  xfree (rows);
  xfree (rgba);
  return ok;
}

bool
ohos_png_image_p (Lisp_Object spec)
{
  return (STRINGP (Fplist_get (spec, Qohos_png_file, Qnil))
	  || STRINGP (Fplist_get (spec, Qohos_png_data, Qnil)));
}

bool
ohos_png_load (struct frame *f, struct image *img)
{
  Lisp_Object file = Fplist_get (img->spec, Qohos_png_file, Qnil);
  Lisp_Object data = Fplist_get (img->spec, Qohos_png_data, Qnil);
  const unsigned char *bytes = NULL;
  unsigned char *owned = NULL;
  size_t len = 0;
  int w = 0, h = 0;
  unsigned char *rgba = NULL;

  (void) f;
  if (STRINGP (file))
    {
      owned = read_whole_file (SSDATA (file), &len);
      if (!owned)
	return false;
      bytes = owned;
    }
  else if (STRINGP (data))
    {
      bytes = SDATA (data);
      len = SBYTES (data);
    }
  else
    return false;

  if (!png_decode (bytes, len, &w, &h, &rgba))
    {
      xfree (owned);
      return false;
    }
  xfree (owned);

  img->lisp_data = make_unibyte_string ((char *) rgba, (ptrdiff_t) w * h * 4);
  xfree (rgba);
  img->width = w;
  img->height = h;
  return true;
}

void
syms_of_ohosimage (void)
{
  Qohos_png_file = intern_c_string (":file");
  staticpro (&Qohos_png_file);
  Qohos_png_data = intern_c_string (":data");
  staticpro (&Qohos_png_data);
}
