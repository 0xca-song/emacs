/* OHOS native GUI redisplay backend.

   This is the display-engine half of the native window backend: the
   redisplay_interface consumes Emacs glyph rows and forwards cells to
   the OHOS draw command queue, where the ArkUI thread drains and
   presents them with OH_Drawing.

   Window management that only ArkUI can perform (title, resize, move,
   fullscreen, menus, notifications) is forwarded through the host
   command channel declared in ohosgui.h.  */

#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
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
#include "coding.h"
#include "termchar.h"
#include "blockinput.h"
#include "ohosgui.h"

/* Cursor handles.  The drawing side only needs a stable identifier, so
   the values are small integers interpreted by the ArkUI page.  */

enum ohos_cursor_id
{
  OHOS_CURSOR_NONE = 0,
  OHOS_CURSOR_TEXT,
  OHOS_CURSOR_NONTEXT,
  OHOS_CURSOR_MODELINE,
  OHOS_CURSOR_HAND,
  OHOS_CURSOR_HOURGLASS,
  OHOS_CURSOR_HDRAG,
  OHOS_CURSOR_VDRAG,
  OHOS_CURSOR_LEFT_RIGHT,
  OHOS_CURSOR_UP_DOWN,
  OHOS_CURSOR_LEFT_EDGE,
  OHOS_CURSOR_TOP_LEFT_CORNER,
  OHOS_CURSOR_TOP_EDGE,
  OHOS_CURSOR_TOP_RIGHT_CORNER,
  OHOS_CURSOR_RIGHT_EDGE,
  OHOS_CURSOR_BOTTOM_RIGHT_CORNER,
  OHOS_CURSOR_BOTTOM_EDGE,
  OHOS_CURSOR_BOTTOM_LEFT_CORNER
};

/* ------------------------------------------------------------------ */
/* Geometry parsing.

   `Fx_parse_geometry' calls this for every geometry string, so OHOS
   needs a real implementation.  XParseGeometry parses
   "[=][<width>{xX}<height>][{+-}<xoffset>{+-}<yoffset>]" and returns a
   bitmask of XValue, YValue, WidthValue, HeightValue, XNegative and
   YNegative.  */

int
XParseGeometry (const char *string, int *x, int *y, unsigned int *width,
		unsigned int *height)
{
  const char *p = string;
  int mask = 0;

  if (!string)
    return 0;

  if (*p == '=')
    p++;

  if (*p >= '0' && *p <= '9')
    {
      char *end;
      unsigned long value = strtoul (p, &end, 10);
      if (end == p)
	return mask;
      if (width)
	*width = (unsigned int) value;
      mask |= WidthValue;
      p = end;
    }

  if (*p == 'x' || *p == 'X')
    {
      p++;
      if (*p >= '0' && *p <= '9')
	{
	  char *end;
	  unsigned long value = strtoul (p, &end, 10);
	  if (end == p)
	    return mask;
	  if (height)
	    *height = (unsigned int) value;
	  mask |= HeightValue;
	  p = end;
	}
    }

  if (*p == '+' || *p == '-')
    {
      bool negative = (*p == '-');
      p++;
      if (*p >= '0' && *p <= '9')
	{
	  char *end;
	  long value = strtol (p, &end, 10);
	  if (end == p)
	    return mask;
	  if (x)
	    *x = negative ? -(int) value : (int) value;
	  mask |= XValue;
	  if (negative)
	    mask |= XNegative;
	  p = end;
	}
    }

  if (*p == '+' || *p == '-')
    {
      bool negative = (*p == '-');
      p++;
      if (*p >= '0' && *p <= '9')
	{
	  char *end;
	  long value = strtol (p, &end, 10);
	  if (end == p)
	    return mask;
	  if (y)
	    *y = negative ? -(int) value : (int) value;
	  mask |= YValue;
	  if (negative)
	    mask |= YNegative;
	  p = end;
	}
    }

  return mask;
}

/* ------------------------------------------------------------------ */
/* Basic drawing.                                                      */

static void
ohos_draw_glyph_string (struct glyph_string *s)
{
  if (!s || !s->f || !s->first_glyph || !s->face)
    return;
  /* Automatic compositions report NCHARS == 0: the glyph count lives in
     the composition, not in the string.  They still have to reach the
     font driver, which draws from s->first_glyph.  */
  if (s->nchars <= 0
      && !(s->first_glyph->type == COMPOSITE_GLYPH
	   && s->first_glyph->u.cmp.automatic))
    return;
  if (s->font && s->font->driver && s->font->driver->draw)
    s->font->driver->draw (s, 0, s->nchars, s->x, s->y, true);
}

static void
ohos_clear_frame_area_rect (struct frame *f, int x, int y, int width,
			    int height)
{
  ohos_draw_fill (x, y, width, height, (uint32_t) FRAME_BACKGROUND_PIXEL (f));
}

static void
ohos_clear_frame_area (struct frame *f, int x, int y, int width, int height)
{
  ohos_clear_frame_area_rect (f, x, y, width, height);
}

static void
ohos_clear_frame (struct frame *f)
{
  static int calls;
  if ((calls++ % 20) == 0)
    ohos_log ("ohos_clear_frame");
  ohos_draw_clear ((uint32_t) FRAME_BACKGROUND_PIXEL (f));
}

static void
ohos_flush (struct frame *f)
{
  static int calls;
  if ((calls++ % 20) == 0)
    {
      char buf[160];
      struct frame *sf = SELECTED_FRAME ();
      snprintf (buf, sizeof buf, "ohos_flush frame=%p selected=%p live=%d",
		(void *) f, (void *) sf, FRAME_LIVE_P (f));
      ohos_log (buf);
    }
  ohos_draw_present ();
}

static void
ohos_after_update_window_line (struct window *w, struct glyph_row *row)
{
  (void) w; (void) row;
}

static void
ohos_scroll_run (struct window *w, struct run *run)
{
  int x, y, width, height, from_y, to_y, bottom_y;

  window_box (w, ANY_AREA, &x, &y, &width, &height);
  from_y = WINDOW_TO_FRAME_PIXEL_Y (w, run->current_y);
  to_y = WINDOW_TO_FRAME_PIXEL_Y (w, run->desired_y);
  bottom_y = y + height;

  if (to_y < from_y)
    {
      if (from_y + run->height > bottom_y)
	height = bottom_y - from_y;
      else
	height = run->height;
      if (to_y < y)
	{
	  int d = y - to_y;
	  height -= d;
	  to_y += d;
	  from_y += d;
	}
    }
  else
    {
      if (to_y + run->height > bottom_y)
	height = bottom_y - to_y;
      else
	height = run->height;
    }
  if (height <= 0)
    return;

  gui_clear_cursor (w);
  ohos_draw_scroll (x, from_y, width, height, to_y);
}

static void
ohos_draw_window_cursor (struct window *w, struct glyph_row *row,
                         int x, int y, enum text_cursor_kinds type,
                         int width, bool on_p, bool active_p)
{
  (void) row; (void) type; (void) active_p;
  if (!w)
    return;
  struct frame *f = WINDOW_XFRAME (w);
  int line_height = FRAME_LINE_HEIGHT (f);
  int fx, fy, cw;

  if (line_height <= 0)
    return;
  if (width <= 0)
    width = FRAME_COLUMN_WIDTH (f);
  cw = width;
  fx = WINDOW_TEXT_TO_FRAME_PIXEL_X (w, x);
  fy = WINDOW_TOP_EDGE_Y (w) + y;

  if (!on_p)
    {
      /* Paint the cursor cell background so the previous glyph shows
	 through again.  The glyph itself is redrawn by redisplay.  */
      ohos_draw_fill (fx, fy, cw, line_height,
		      (uint32_t) FRAME_BACKGROUND_PIXEL (f));
      return;
    }
  ohos_draw_cursor (fx, fy, cw, line_height,
		    (uint32_t) FRAME_FOREGROUND_PIXEL (f),
		    (uint32_t) FRAME_BACKGROUND_PIXEL (f),
		    1u);
}

static void
ohos_draw_vertical_window_border (struct window *w, int x, int y0, int y1)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct face *face = FACE_FROM_ID_OR_NULL (f, VERTICAL_BORDER_FACE_ID);
  unsigned long color = (face ? face->foreground
			 : FRAME_FOREGROUND_PIXEL (f));

  if (y1 <= y0)
    return;
  ohos_draw_fill (x, y0, 1, y1 - y0, (uint32_t) color);
}

static void
ohos_draw_window_divider (struct window *w, int x0, int x1, int y0, int y1)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct face *face = FACE_FROM_ID_OR_NULL (f, WINDOW_DIVIDER_FACE_ID);
  struct face *face_first
    = FACE_FROM_ID_OR_NULL (f, WINDOW_DIVIDER_FIRST_PIXEL_FACE_ID);
  struct face *face_last
    = FACE_FROM_ID_OR_NULL (f, WINDOW_DIVIDER_LAST_PIXEL_FACE_ID);
  unsigned long color = face ? face->foreground : FRAME_FOREGROUND_PIXEL (f);
  unsigned long color_first = (face_first
			       ? face_first->foreground
			       : FRAME_FOREGROUND_PIXEL (f));
  unsigned long color_last = (face_last
			      ? face_last->foreground
			      : FRAME_FOREGROUND_PIXEL (f));

  if (x1 - x0 <= 0 || y1 - y0 <= 0)
    return;
  if ((y1 - y0 > x1 - x0) && (x1 - x0 >= 3))
    {
      /* A vertical divider, at least three pixels wide: draw the first
	 and last pixels differently.  */
      ohos_draw_fill (x0, y0, 1, y1 - y0, (uint32_t) color_first);
      ohos_draw_fill (x0 + 1, y0, x1 - x0 - 2, y1 - y0, (uint32_t) color);
      ohos_draw_fill (x1 - 1, y0, 1, y1 - y0, (uint32_t) color_last);
    }
  else if ((x1 - x0 > y1 - y0) && (y1 - y0 >= 3))
    {
      /* A horizontal divider, at least three pixels high.  */
      ohos_draw_fill (x0, y0, x1 - x0, 1, (uint32_t) color_first);
      ohos_draw_fill (x0, y0 + 1, x1 - x0, y1 - y0 - 2, (uint32_t) color);
      ohos_draw_fill (x0, y1 - 1, x1 - x0, 1, (uint32_t) color_last);
    }
  else
    ohos_draw_fill (x0, y0, x1 - x0, y1 - y0, (uint32_t) color);
}

static void
ohos_clear_under_internal_border (struct frame *f)
{
  if (FRAME_INTERNAL_BORDER_WIDTH (f) > 0)
    {
      int border = FRAME_INTERNAL_BORDER_WIDTH (f);
      int width = FRAME_PIXEL_WIDTH (f);
      int height = FRAME_PIXEL_HEIGHT (f);
      int margin = FRAME_TOP_MARGIN_HEIGHT (f);
      int bottom_margin = FRAME_BOTTOM_MARGIN_HEIGHT (f);
      int face_id = (FRAME_PARENT_FRAME (f)
		     ? (!NILP (Vface_remapping_alist)
			? lookup_basic_face (NULL, f,
					     CHILD_FRAME_BORDER_FACE_ID)
			: CHILD_FRAME_BORDER_FACE_ID)
		     : (!NILP (Vface_remapping_alist)
			? lookup_basic_face (NULL, f,
					     INTERNAL_BORDER_FACE_ID)
			: INTERNAL_BORDER_FACE_ID));
      struct face *face = FACE_FROM_ID_OR_NULL (f, face_id);
      unsigned long color = (face ? face->background
			       : FRAME_BACKGROUND_PIXEL (f));

      ohos_draw_fill (0, margin, width, border, (uint32_t) color);
      ohos_draw_fill (0, 0, border, height, (uint32_t) color);
      ohos_draw_fill (width - border, 0, border, height, (uint32_t) color);
      if (height - bottom_margin - border >= 0)
	ohos_draw_fill (0, height - bottom_margin - border, width, border,
			(uint32_t) color);
    }
}

/* Map a cursor identifier to the name the host understands.  */

static const char *
ohos_cursor_name (Emacs_Cursor cursor)
{
  switch ((int) (intptr_t) cursor)
    {
    case OHOS_CURSOR_TEXT: return "text";
    case OHOS_CURSOR_NONTEXT: return "text";
    case OHOS_CURSOR_MODELINE: return "text";
    case OHOS_CURSOR_HAND: return "hand";
    case OHOS_CURSOR_HOURGLASS: return "hourglass";
    case OHOS_CURSOR_HDRAG: return "hdrag";
    case OHOS_CURSOR_VDRAG: return "vdrag";
    case OHOS_CURSOR_LEFT_RIGHT: return "leftright";
    case OHOS_CURSOR_UP_DOWN: return "updown";
    case OHOS_CURSOR_LEFT_EDGE: return "leftright";
    case OHOS_CURSOR_RIGHT_EDGE: return "leftright";
    case OHOS_CURSOR_TOP_EDGE: return "updown";
    case OHOS_CURSOR_BOTTOM_EDGE: return "updown";
    case OHOS_CURSOR_TOP_LEFT_CORNER:
    case OHOS_CURSOR_TOP_RIGHT_CORNER:
    case OHOS_CURSOR_BOTTOM_RIGHT_CORNER:
    case OHOS_CURSOR_BOTTOM_LEFT_CORNER: return "text";
    case OHOS_CURSOR_NONE: return "none";
    default: return "text";
    }
}

static void
ohos_define_frame_cursor (struct frame *f, Emacs_Cursor cursor)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);

  if (f->pointer_invisible || o->hourglass)
    return;
  if (o->current_cursor == cursor)
    return;
  o->current_cursor = cursor;
  ohos_host_command ("cursor %s", ohos_cursor_name (cursor));
}

/* Fill S's overhang fields from the font metrics.  */

static void
ohos_compute_glyph_string_overhangs (struct glyph_string *s)
{
  if (s->cmp == NULL
      && (s->first_glyph->type == CHAR_GLYPH
	  || s->first_glyph->type == COMPOSITE_GLYPH))
    {
      struct font_metrics metrics;

      if (s->first_glyph->type == CHAR_GLYPH)
	{
	  struct font *font = s->font;
	  if (font && font->driver && font->driver->text_extents)
	    font->driver->text_extents (font, s->char2b, s->nchars,
					&metrics);
	  else
	    memset (&metrics, 0, sizeof metrics);
	}
      else
	{
	  Lisp_Object gstring = composition_gstring_from_id (s->cmp_id);
	  composition_gstring_width (gstring, s->cmp_from, s->cmp_to,
				     &metrics);
	}
      s->right_overhang = (metrics.rbearing > metrics.width
			   ? metrics.rbearing - metrics.width : 0);
      s->left_overhang = metrics.lbearing < 0 ? -metrics.lbearing : 0;
    }
  else if (s->cmp)
    {
      s->right_overhang = s->cmp->rbearing - s->cmp->pixel_width;
      s->left_overhang = -s->cmp->lbearing;
    }
}

/* ------------------------------------------------------------------ */
/* Fringes.                                                            */

/* Fringe bitmaps are plain bit arrays.  Draw them as horizontal runs of
   foreground pixels so no bitmap upload path is needed.  */

static void
ohos_draw_fringe_bitmap (struct window *w, struct glyph_row *row,
			 struct draw_fringe_bitmap_params *p)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct face *face = p->face;
  unsigned long fg = face ? face->foreground : FRAME_FOREGROUND_PIXEL (f);
  unsigned long bg = face ? face->background : FRAME_BACKGROUND_PIXEL (f);
  int y;

  (void) row;
  if (p->cursor_p)
    {
      unsigned long cursor = FRAME_OUTPUT_DATA (f)->cursor_pixel;
      if (cursor != (unsigned long) -1)
	fg = cursor;
    }

  if (p->bx >= 0 && !p->overlay_p)
    ohos_draw_fill (p->bx, p->by, p->nx, p->ny, (uint32_t) bg);

  if (!p->which || !p->bits)
    return;

  for (y = 0; y < p->h; y++)
    {
      int run_start = -1;
      int x;

      for (x = 0; x <= p->wd; x++)
	{
	  bool on = false;

	  if (x < p->wd)
	    {
	      if (p->wd > 8)
		{
		  const unsigned char *rowbits
		    = ((const unsigned char *) p->bits + p->dh)
		    + y * ((p->wd + 7) / 8);
		  on = (rowbits[x / 8] >> (x % 8)) & 1;
		}
	      else
		{
		  const unsigned char *rowbits
		    = (const unsigned char *) p->bits + p->dh;
		  on = (rowbits[y] >> x) & 1;
		}
	    }
	  if (on && run_start < 0)
	    run_start = x;
	  else if (!on && run_start >= 0)
	    {
	      ohos_draw_fill (p->x + run_start, p->y + y, x - run_start, 1,
			      (uint32_t) fg);
	      run_start = -1;
	    }
	}
    }
}

static void
ohos_define_fringe_bitmap (int which, unsigned short *bits, int h, int wd)
{
  /* The generic fringe code owns the bitmap storage and
     ohos_draw_fringe_bitmap reads it directly, so there is nothing to
     copy here.  */
  (void) which; (void) bits; (void) h; (void) wd;
}

static void
ohos_destroy_fringe_bitmap (int which)
{
  (void) which;
}

/* ------------------------------------------------------------------ */
/* Insertions, hourglass and per-window update hooks.                  */

/* Shift the pixels in the region right by SHIFT_BY.

   The draw protocol has no horizontal blit, so the area is cleared and
   the frame marked garbaged: the caller draws the new glyphs on top,
   and the remaining content is repainted by the next redisplay pass.  */

static void
ohos_shift_glyphs_for_insert (struct frame *f, int x, int y, int width,
			      int height, int shift_by)
{
  (void) shift_by;
  if (width > 0 && height > 0)
    ohos_clear_frame_area_rect (f, x, y, width, height);
  SET_FRAME_GARBAGED (f);
}

static void
ohos_show_hourglass (struct frame *f)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);

  if (popup_activated ())
    return;
  o->hourglass = true;
  ohos_host_command ("hourglass 1");
}

static void
ohos_hide_hourglass (struct frame *f)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);

  if (!o->hourglass)
    return;
  o->hourglass = false;
  ohos_host_command ("hourglass 0");
  if (!f->pointer_invisible)
    ohos_define_frame_cursor (f, o->text_cursor);
}

static void
ohos_update_window_begin (struct window *w)
{
  struct frame *f = XFRAME (w->frame);

  FRAME_OUTPUT_DATA (f)->update_in_progress = true;
}

static void
ohos_update_window_end (struct window *w, bool cursor_on_p,
			bool mouse_face_overwritten_p)
{
  struct frame *f = XFRAME (w->frame);
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);

  o->update_in_progress = false;
  if (mouse_face_overwritten_p)
    MOUSE_HL_INFO (f)->mouse_face_defer = false;
  if (cursor_on_p)
    gui_update_cursor (f, true);
}

/* ------------------------------------------------------------------ */
/* Scroll bars.                                                        */

/* Scroll bars are painted with the normal draw command stream: the
   rectangle is filled with the scroll bar background, then the handle
   is filled with the scroll bar foreground.  */

static uint32_t
ohos_scroll_bar_color (struct frame *f, Lisp_Object param, uint32_t dflt)
{
  Lisp_Object value = get_frame_param (f, param);
  Emacs_Color color;

  if (STRINGP (value)
      && ohos_defined_color (f, SSDATA (value), &color, false, false))
    return (uint32_t) color.pixel;
  return dflt;
}

static struct ohos_scroll_bar *
ohos_find_scroll_bar (struct frame *f, struct window *w)
{
  for (struct ohos_scroll_bar *bar = FRAME_OUTPUT_DATA (f)->scroll_bars;
       bar; bar = bar->next)
    if (bar->window == w)
      return bar;
  return NULL;
}

static void
ohos_draw_one_scroll_bar (struct frame *f, struct ohos_scroll_bar *bar)
{
  uint32_t bg = ohos_scroll_bar_color (f, Qohos_scroll_bar_background,
				       (uint32_t) FRAME_BACKGROUND_PIXEL (f));
  uint32_t fg = ohos_scroll_bar_color (f, Qohos_scroll_bar_foreground,
				       (uint32_t) FRAME_FOREGROUND_PIXEL (f));
  int length = bar->horizontal ? bar->width : bar->height;
  int handle_size, handle_start, denominator;

  if (bar->width <= 0 || bar->height <= 0 || length <= 0)
    return;

  ohos_draw_fill (bar->x, bar->y, bar->width, bar->height, bg);
  if (bar->whole <= 0 || bar->portion <= 0)
    return;

  handle_size = (int) ((double) length * bar->portion / bar->whole + 0.5);
  if (handle_size < 8)
    handle_size = 8;
  if (handle_size > length)
    handle_size = length;
  denominator = (bar->whole > bar->portion
		 ? bar->whole - bar->portion : 1);
  handle_start = (int) ((double) (length - handle_size) * bar->position
			/ denominator + 0.5);
  if (handle_start < 0)
    handle_start = 0;
  if (handle_start + handle_size > length)
    handle_start = length - handle_size;

  if (bar->horizontal)
    ohos_draw_fill (bar->x + handle_start, bar->y, handle_size, bar->height,
		    fg);
  else
    ohos_draw_fill (bar->x, bar->y + handle_start, bar->width, handle_size,
		    fg);
}

static void
ohos_release_scroll_bar (struct frame *f, struct ohos_scroll_bar *bar)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);
  struct ohos_scroll_bar **link = &o->scroll_bars;

  if (bar->width > 0 && bar->height > 0)
    ohos_clear_frame_area_rect (f, bar->x, bar->y, bar->width, bar->height);

  while (*link)
    {
      if (*link == bar)
	{
	  *link = bar->next;
	  break;
	}
      link = &(*link)->next;
    }
  xfree (bar);
}

static struct ohos_scroll_bar *
ohos_alloc_scroll_bar (struct window *w)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);
  struct ohos_scroll_bar *bar = xzalloc (sizeof *bar);

  bar->window = w;
  bar->next = o->scroll_bars;
  o->scroll_bars = bar;
  return bar;
}

static void
ohos_set_vertical_scroll_bar (struct window *w, int portion, int whole,
			      int position)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct ohos_scroll_bar *bar = ohos_find_scroll_bar (f, w);
  int top, height, left, width, window_y, window_height;

  eassert (WINDOW_HAS_VERTICAL_SCROLL_BAR (w));

  window_box (w, ANY_AREA, 0, &window_y, 0, &window_height);
  top = window_y;
  height = window_height;
  left = WINDOW_SCROLL_BAR_AREA_X (w);
  width = WINDOW_SCROLL_BAR_AREA_WIDTH (w);

  if (!bar)
    {
      bar = ohos_alloc_scroll_bar (w);
      bar->horizontal = false;
      wset_vertical_scroll_bar (w, Qt);
    }
  else if (bar->x != left || bar->y != top
	   || bar->width != width || bar->height != height)
    {
      if (bar->width > 0 && bar->height > 0)
	ohos_clear_frame_area_rect (f, bar->x, bar->y, bar->width,
				    bar->height);
    }

  bar->x = left;
  bar->y = top;
  bar->width = width;
  bar->height = height;
  bar->portion = portion;
  bar->whole = whole;
  bar->position = position;
  bar->condemned = false;
  ohos_draw_one_scroll_bar (f, bar);
}

static void
ohos_set_horizontal_scroll_bar (struct window *w, int portion, int whole,
				int position)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));
  struct ohos_scroll_bar *bar = ohos_find_scroll_bar (f, w);
  int left, width, top, height, window_x, window_width;

  eassert (WINDOW_HAS_HORIZONTAL_SCROLL_BAR (w));

  window_box (w, ANY_AREA, &window_x, 0, &window_width, 0);
  left = window_x;
  width = window_width;
  top = WINDOW_BOTTOM_EDGE_Y (w) - WINDOW_SCROLL_BAR_AREA_HEIGHT (w);
  height = WINDOW_SCROLL_BAR_AREA_HEIGHT (w);

  if (!bar)
    {
      bar = ohos_alloc_scroll_bar (w);
      bar->horizontal = true;
      wset_horizontal_scroll_bar (w, Qt);
    }
  else if (bar->x != left || bar->y != top
	   || bar->width != width || bar->height != height)
    {
      if (bar->width > 0 && bar->height > 0)
	ohos_clear_frame_area_rect (f, bar->x, bar->y, bar->width,
				    bar->height);
    }

  bar->x = left;
  bar->y = top;
  bar->width = width;
  bar->height = height;
  bar->portion = portion;
  bar->whole = whole;
  bar->position = position;
  bar->condemned = false;
  ohos_draw_one_scroll_bar (f, bar);
}

static void
ohos_set_scroll_bar_default_width (struct frame *f)
{
  int unit = FRAME_COLUMN_WIDTH (f);
  int size = 14;

  FRAME_CONFIG_SCROLL_BAR_COLS (f) = (size + unit - 1) / unit;
  FRAME_CONFIG_SCROLL_BAR_WIDTH (f)
    = FRAME_CONFIG_SCROLL_BAR_COLS (f) * unit;
}

static void
ohos_set_scroll_bar_default_height (struct frame *f)
{
  int height = FRAME_LINE_HEIGHT (f);
  int size = 14;

  FRAME_CONFIG_SCROLL_BAR_LINES (f) = (size + height - 1) / height;
  FRAME_CONFIG_SCROLL_BAR_HEIGHT (f) = size;
}

static void
ohos_condemn_scroll_bars (struct frame *f)
{
  for (struct ohos_scroll_bar *bar = FRAME_OUTPUT_DATA (f)->scroll_bars;
       bar; bar = bar->next)
    bar->condemned = true;
}

static void
ohos_redeem_scroll_bar (struct window *w)
{
  struct frame *f = XFRAME (WINDOW_FRAME (w));

  for (struct ohos_scroll_bar *bar = FRAME_OUTPUT_DATA (f)->scroll_bars;
       bar; bar = bar->next)
    if (bar->window == w)
      bar->condemned = false;
}

static void
ohos_judge_scroll_bars (struct frame *f)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);
  struct ohos_scroll_bar *bar, *next;

  for (bar = o->scroll_bars; bar; bar = next)
    {
      next = bar->next;
      if (!bar->condemned)
	continue;
      /* The window may have been deleted together with its bar; only
	 touch windows that are still live.  */
      if (bar->window && !NILP (bar->window->contents))
	{
	  if (WINDOW_HAS_VERTICAL_SCROLL_BAR (bar->window))
	    wset_vertical_scroll_bar (bar->window, Qnil);
	  if (WINDOW_HAS_HORIZONTAL_SCROLL_BAR (bar->window))
	    wset_horizontal_scroll_bar (bar->window, Qnil);
	}
      ohos_release_scroll_bar (f, bar);
    }
}

/* ------------------------------------------------------------------ */
/* Mouse, colors, focus.                                               */

static void
ohos_query_colors (struct frame *f, Emacs_Color *colors, int ncolors)
{
  int i;

  (void) f;
  for (i = 0; i < ncolors; ++i)
    {
      unsigned long pixel = colors[i].pixel;
      colors[i].red = ((pixel >> 16) & 0xff) * 0x101;
      colors[i].green = ((pixel >> 8) & 0xff) * 0x101;
      colors[i].blue = (pixel & 0xff) * 0x101;
    }
}

static void
ohos_mouse_position (struct frame **fp, int insist, Lisp_Object *bar_window,
		     enum scroll_bar_part *part, Lisp_Object *x,
		     Lisp_Object *y, Time *timestamp)
{
  struct frame *f = SELECTED_FRAME ();
  struct ohos_display_info *dpyinfo;
  int mx, my;

  (void) insist;
  if (!f || !FRAME_OHOS_P (f))
    {
      *fp = NULL;
      *bar_window = Qnil;
      if (timestamp)
	*timestamp = 0;
      return;
    }
  dpyinfo = FRAME_DISPLAY_INFO (f);
  mx = dpyinfo->last_mouse_motion_x;
  my = dpyinfo->last_mouse_motion_y;

  for (struct ohos_scroll_bar *bar = FRAME_OUTPUT_DATA (f)->scroll_bars;
       bar; bar = bar->next)
    {
      if (mx >= bar->x && mx < bar->x + bar->width
	  && my >= bar->y && my < bar->y + bar->height)
	{
	  int length = bar->horizontal ? bar->width : bar->height;
	  int handle_size = (int) ((double) length
				   * bar->portion / bar->whole + 0.5);
	  int denominator = (bar->whole > bar->portion
			     ? bar->whole - bar->portion : 1);
	  int start, pos = bar->horizontal ? mx - bar->x : my - bar->y;

	  if (handle_size > length)
	    handle_size = length;
	  start = (int) ((double) (length - handle_size) * bar->position
			 / denominator + 0.5);
	  *fp = f;
	  XSETWINDOW (*bar_window, bar->window);
	  if (pos < start)
	    *part = scroll_bar_above_handle;
	  else if (pos < start + handle_size)
	    *part = scroll_bar_handle;
	  else
	    *part = scroll_bar_below_handle;
	  *x = make_fixnum (pos);
	  *y = make_fixnum (length);
	  if (timestamp)
	    *timestamp = 0;
	  return;
	}
    }

  *fp = f;
  *bar_window = Qnil;
  *part = scroll_bar_nowhere;
  *x = make_fixnum (FRAME_COLUMN_WIDTH (f) > 0
		    ? mx / FRAME_COLUMN_WIDTH (f) : 0);
  *y = make_fixnum (FRAME_LINE_HEIGHT (f) > 0
		    ? my / FRAME_LINE_HEIGHT (f) : 0);
  if (timestamp)
    *timestamp = 0;
}

/* Display-side frame highlight.  The mouse face is drawn by redisplay;
   only the cursor needs to be refreshed here.  */

static void
ohos_frame_highlight (struct frame *f)
{
  gui_update_cursor (f, true);
}

static void
ohos_frame_unhighlight (struct frame *f)
{
  gui_update_cursor (f, false);
}

static void
ohos_frame_rehighlight (struct ohos_display_info *dpyinfo)
{
  struct frame *old_highlight = dpyinfo->highlight_frame;

  if (!NILP (dpyinfo->focus_frame) && FRAMEP (dpyinfo->focus_frame)
      && FRAME_LIVE_P (XFRAME (dpyinfo->focus_frame)))
    {
      dpyinfo->highlight_frame
	= ((FRAMEP (FRAME_FOCUS_FRAME (XFRAME (dpyinfo->focus_frame))))
	   ? XFRAME (FRAME_FOCUS_FRAME (XFRAME (dpyinfo->focus_frame)))
	   : XFRAME (dpyinfo->focus_frame));
      if (!FRAME_LIVE_P (dpyinfo->highlight_frame))
	{
	  fset_focus_frame (XFRAME (dpyinfo->focus_frame), Qnil);
	  dpyinfo->highlight_frame = XFRAME (dpyinfo->focus_frame);
	}
    }
  else
    dpyinfo->highlight_frame = NULL;

  if (dpyinfo->highlight_frame != old_highlight)
    {
      if (old_highlight)
	ohos_frame_unhighlight (old_highlight);
      if (dpyinfo->highlight_frame)
	ohos_frame_highlight (dpyinfo->highlight_frame);
    }
}

static void
ohos_frame_rehighlight_hook (struct frame *f)
{
  ohos_frame_rehighlight (FRAME_DISPLAY_INFO (f));
}

static Lisp_Object
ohos_get_focus_frame (struct frame *f)
{
  Lisp_Object focus = FRAME_DISPLAY_INFO (f)->focus_frame;

  if (!FRAMEP (focus) || !FRAME_LIVE_P (XFRAME (focus)))
    return Qnil;
  return focus;
}

static void
ohos_focus_frame (struct frame *f, bool noactivate)
{
  struct ohos_display_info *dpyinfo = FRAME_DISPLAY_INFO (f);
  Lisp_Object old_focus = dpyinfo->focus_frame;
  Lisp_Object frame;

  (void) noactivate;
  XSETFRAME (frame, f);
  if (!EQ (old_focus, frame))
    {
      /* ArkUI owns the input focus; this keeps the Emacs bookkeeping
	 and the mouse highlight in sync with it.  */
      dpyinfo->focus_frame = frame;
      ohos_frame_rehighlight (dpyinfo);
    }
}

static void
ohos_frame_raise_lower (struct frame *f, bool raise_flag)
{
  (void) f;
  ohos_host_command (raise_flag ? "raise" : "lower");
}

static void
ohos_set_frame_alpha (struct frame *f)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);
  double alpha = f->alpha[0] < 0 ? 1.0 : f->alpha[0];
  int percent = (int) (alpha * 100.0 + 0.5);

  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;
  if (o->alpha_hint == percent)
    return;
  o->alpha_hint = percent;
  ohos_host_command ("alpha %d", percent);
}

static void
ohos_ring_bell (struct frame *f)
{
  if (visible_bell)
    {
      ohos_draw_fill (0, 0, FRAME_PIXEL_WIDTH (f), FRAME_PIXEL_HEIGHT (f),
		      (uint32_t) FRAME_FOREGROUND_PIXEL (f));
      ohos_draw_present ();
    }
  block_input ();
  ohos_host_command ("bell");
  unblock_input ();
}

static void
ohos_toggle_invisible_pointer (struct frame *f, bool invisible)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);

  block_input ();
  f->pointer_invisible = invisible;
  o->current_cursor = invisible ? (Emacs_Cursor) OHOS_CURSOR_NONE
    : (Emacs_Cursor) OHOS_CURSOR_TEXT;
  ohos_host_command ("cursor %s", invisible ? "none" : "text");
  unblock_input ();
}

static void
ohos_update_begin (struct frame *f)
{
  FRAME_OUTPUT_DATA (f)->complete_p = false;
}

static void
ohos_update_end (struct frame *f)
{
  if (!f->pointer_invisible && !FRAME_OUTPUT_DATA (f)->hourglass)
    MOUSE_HL_INFO (f)->mouse_face_defer = false;
}

static void
ohos_frame_up_to_date (struct frame *f)
{
  block_input ();
  FRAME_MOUSE_UPDATE (f);
  FRAME_OUTPUT_DATA (f)->complete_p = true;
  unblock_input ();
}

static void
ohos_buffer_flipping_unblocked (struct frame *f)
{
  /* ohos_flush presents synchronously, so no flip can be pending.  */
  (void) f;
}

static void
ohos_free_pixmap (struct frame *f, Emacs_Pixmap pixmap)
{
  /* Bitmaps live on the ArkUI drawing surface; nothing to free.  */
  (void) f; (void) pixmap;
}

static void
ohos_toolkit_position (struct frame *f, int x, int y,
		       bool *menu_bar_p, bool *tool_bar_p)
{
  /* Menu bar and tool bar clicks are routed through the glyph rows by
     the generic redisplay code, so no toolkit hit test is needed.  */
  (void) f; (void) x; (void) y;
  if (menu_bar_p)
    *menu_bar_p = false;
  if (tool_bar_p)
    *tool_bar_p = false;
}

static bool
ohos_any_grab (Display_Info *dpyinfo)
{
  return dpyinfo && dpyinfo->grabbed;
}

/* ------------------------------------------------------------------ */
/* Tab bar / tool bar heights.                                         */

void
ohos_change_tool_bar_height (struct frame *f, int height)
{
  int unit = FRAME_LINE_HEIGHT (f);
  int old_height = FRAME_TOOL_BAR_HEIGHT (f);
  int lines = (height + unit - 1) / unit;
  Lisp_Object fullscreen = get_frame_param (f, Qfullscreen);

  fset_redisplay (f);
  FRAME_TOOL_BAR_HEIGHT (f) = height;
  FRAME_TOOL_BAR_LINES (f) = lines;
  store_frame_param (f, Qtool_bar_lines, make_fixnum (lines));

  if (FRAME_TOOL_BAR_HEIGHT (f) == 0)
    {
      clear_frame (f);
      clear_current_matrices (f);
    }
  if ((height < old_height) && WINDOWP (f->tool_bar_window))
    clear_glyph_matrix (XWINDOW (f->tool_bar_window)->current_matrix);

  if (!f->tool_bar_resized)
    {
      if (NILP (fullscreen) || EQ (fullscreen, Qfullwidth))
	adjust_frame_size (f, FRAME_TEXT_WIDTH (f), FRAME_TEXT_HEIGHT (f),
			   1, false, Qtool_bar_lines);
      else
	adjust_frame_size (f, -1, -1, 4, false, Qtool_bar_lines);
      f->tool_bar_resized = f->tool_bar_redisplayed;
    }
  else
    adjust_frame_size (f, -1, -1, 3, false, Qtool_bar_lines);

  adjust_frame_glyphs (f);
  SET_FRAME_GARBAGED (f);
}

void
ohos_change_tab_bar_height (struct frame *f, int height)
{
  int unit = FRAME_LINE_HEIGHT (f);
  int old_height = FRAME_TAB_BAR_HEIGHT (f);
  Lisp_Object fullscreen = get_frame_param (f, Qfullscreen);
  int lines = height / unit;

  if (lines == 0 && height != 0)
    lines = 1;

  fset_redisplay (f);
  FRAME_TAB_BAR_HEIGHT (f) = height;
  FRAME_TAB_BAR_LINES (f) = lines;
  store_frame_param (f, Qtab_bar_lines, make_fixnum (lines));

  if (FRAME_TAB_BAR_HEIGHT (f) == 0)
    {
      clear_frame (f);
      clear_current_matrices (f);
    }
  if ((height < old_height) && WINDOWP (f->tab_bar_window))
    clear_glyph_matrix (XWINDOW (f->tab_bar_window)->current_matrix);

  if (!f->tab_bar_resized)
    {
      if (NILP (fullscreen) || EQ (fullscreen, Qfullwidth))
	adjust_frame_size (f, FRAME_TEXT_WIDTH (f), FRAME_TEXT_HEIGHT (f),
			   1, false, Qtab_bar_lines);
      else
	adjust_frame_size (f, -1, -1, 4, false, Qtab_bar_lines);
      f->tab_bar_resized = f->tab_bar_redisplayed;
    }
  else
    adjust_frame_size (f, -1, -1, 3, false, Qtab_bar_lines);

  adjust_frame_glyphs (f);
  SET_FRAME_GARBAGED (f);
}

/* ------------------------------------------------------------------ */
/* Frame / terminal deletion.                                          */

static void
ohos_delete_frame (struct frame *f)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);

  if (!o)
    return;
  while (o->scroll_bars)
    ohos_release_scroll_bar (f, o->scroll_bars);
}

/* OHOS has no X resource database: frame parameters come from Lisp
   only.  The generic frame-parameter code calls this hook
   unconditionally, so it must return NULL rather than be NULL.  */

static const char *
ohos_get_string_resource (void *rdb, const char *name, const char *class)
{
  (void) rdb; (void) name; (void) class;
  return NULL;
}

static void
ohos_delete_terminal (struct terminal *terminal)
{
  struct ohos_display_info *dpyinfo = terminal->display_info.ohos;
  if (!dpyinfo)
    return;
  int fd = ohos_input_wait_fd ();
  if (fd >= 0)
    delete_keyboard_wait_descriptor (fd);
  terminal->display_info.ohos = NULL;
  xfree (dpyinfo);
}

/* ------------------------------------------------------------------ */
/* Surface geometry.                                                   */

/* The ArkUI page reports the XComponent surface size.  Emacs adopts it
   so the frame fills the surface instead of being letterboxed.  */

void
ohos_handle_surface_resize (struct frame *f, int width, int height)
{
  struct ohos_output *o;
  int cols, lines;

  if (!f || !FRAME_LIVE_P (f) || !FRAME_OHOS_P (f) || width <= 0
      || height <= 0)
    return;
  o = FRAME_OUTPUT_DATA (f);
  if (FRAME_COLUMN_WIDTH (f) <= 0 || FRAME_LINE_HEIGHT (f) <= 0)
    return;

  cols = width / FRAME_COLUMN_WIDTH (f);
  lines = height / FRAME_LINE_HEIGHT (f);
  {
    char buf[160];
    snprintf (buf, sizeof buf,
	      "surface_resize px=%dx%d cell=%dx%d cols=%d lines=%d",
	      width, height, FRAME_COLUMN_WIDTH (f), FRAME_LINE_HEIGHT (f),
	      cols, lines);
    ohos_log (buf);
  }
  if (cols < 10)
    cols = 10;
  if (lines < 4)
    lines = 4;

  /* Remember the surface size so ohos_set_window_size does not echo a
     resize command back to the host that reported it.  */
  o->host_surface_width = width;
  o->host_surface_height = height;
  o->surface_seen = true;

  if (cols == FRAME_COLS (f) && lines == FRAME_LINES (f))
    return;
  change_frame_size (f, cols, lines, false, false, false);
}

/* ------------------------------------------------------------------ */
/* Input.                                                              */

/* OHOS KeyCode values used by the ArkUI bridge.  Keep this small and
   local: Emacs must not depend on the ArkUI headers.  */
#define OHOS_KEY_0		2000
#define OHOS_KEY_9		2009
#define OHOS_KEY_DPAD_UP	2012
#define OHOS_KEY_DPAD_DOWN	2013
#define OHOS_KEY_DPAD_LEFT	2014
#define OHOS_KEY_DPAD_RIGHT	2015
#define OHOS_KEY_A		2017
#define OHOS_KEY_Z		2042
#define OHOS_KEY_COMMA		2043
#define OHOS_KEY_PERIOD		2044
#define OHOS_KEY_TAB		2049
#define OHOS_KEY_SPACE		2050
#define OHOS_KEY_ENTER		2054
#define OHOS_KEY_DEL		2055
#define OHOS_KEY_GRAVE		2056
#define OHOS_KEY_MINUS		2057
#define OHOS_KEY_EQUALS		2058
#define OHOS_KEY_LEFT_BRACKET	2059
#define OHOS_KEY_RIGHT_BRACKET	2060
#define OHOS_KEY_BACKSLASH	2061
#define OHOS_KEY_SEMICOLON	2062
#define OHOS_KEY_APOSTROPHE	2063
#define OHOS_KEY_SLASH		2064
#define OHOS_KEY_PAGE_UP	2068
#define OHOS_KEY_PAGE_DOWN	2069
#define OHOS_KEY_ESCAPE		2070
#define OHOS_KEY_FORWARD_DEL	2071
#define OHOS_KEY_MOVE_HOME	2081
#define OHOS_KEY_MOVE_END	2082
#define OHOS_KEY_F1		2090
#define OHOS_KEY_F12		2101
#define OHOS_KEY_DPAD_CENTER	2016
#define OHOS_KEY_NUMPAD_0	2103
#define OHOS_KEY_NUMPAD_9	2112
#define OHOS_KEY_NUMPAD_DIVIDE	2113
#define OHOS_KEY_NUMPAD_MULTIPLY 2114
#define OHOS_KEY_NUMPAD_SUBTRACT 2115
#define OHOS_KEY_NUMPAD_ADD	2116
#define OHOS_KEY_NUMPAD_DOT	2117
#define OHOS_KEY_NUMPAD_COMMA	2118
#define OHOS_KEY_NUMPAD_ENTER	2119
#define OHOS_KEY_NUMPAD_EQUALS	2120

#define OHOS_MOD_SHIFT	1
#define OHOS_MOD_CTRL	2
#define OHOS_MOD_ALT	4
#define OHOS_MOD_META	8
#define OHOS_MOD_CAPS	16

static unsigned
ohos_char_from_key (unsigned code)
{
  if (code >= OHOS_KEY_A && code <= OHOS_KEY_Z)
    return 'a' + (code - OHOS_KEY_A);
  if (code >= OHOS_KEY_0 && code <= OHOS_KEY_9)
    return '0' + (code - OHOS_KEY_0);
  if (code >= OHOS_KEY_NUMPAD_0 && code <= OHOS_KEY_NUMPAD_9)
    return '0' + (code - OHOS_KEY_NUMPAD_0);
  switch (code)
    {
    case OHOS_KEY_SPACE: return ' ';
    case OHOS_KEY_COMMA: return ',';
    case OHOS_KEY_PERIOD: return '.';
    case OHOS_KEY_SLASH: return '/';
    case OHOS_KEY_SEMICOLON: return ';';
    case OHOS_KEY_APOSTROPHE: return '\'';
    case OHOS_KEY_LEFT_BRACKET: return '[';
    case OHOS_KEY_RIGHT_BRACKET: return ']';
    case OHOS_KEY_BACKSLASH: return '\\';
    case OHOS_KEY_GRAVE: return '`';
    case OHOS_KEY_MINUS: return '-';
    case OHOS_KEY_EQUALS: return '=';
    case OHOS_KEY_NUMPAD_DIVIDE: return '/';
    case OHOS_KEY_NUMPAD_MULTIPLY: return '*';
    case OHOS_KEY_NUMPAD_SUBTRACT: return '-';
    case OHOS_KEY_NUMPAD_ADD: return '+';
    case OHOS_KEY_NUMPAD_DOT: return '.';
    case OHOS_KEY_NUMPAD_COMMA: return ',';
    case OHOS_KEY_NUMPAD_EQUALS: return '=';
    default: return 0;
    }
}

static unsigned
ohos_keysym_from_key (unsigned code)
{
  switch (code)
    {
    case OHOS_KEY_ENTER: return 0xff0d;	/* XK_Return */
    case OHOS_KEY_NUMPAD_ENTER: return 0xff0d;
    case OHOS_KEY_DPAD_CENTER: return 0xff0d;
    case OHOS_KEY_DEL: return 0xff08;	/* XK_BackSpace */
    case OHOS_KEY_TAB: return 0xff09;	/* XK_Tab */
    case OHOS_KEY_ESCAPE: return 0xff1b;	/* XK_Escape */
    case OHOS_KEY_FORWARD_DEL: return 0xffff; /* XK_Delete */
    case OHOS_KEY_DPAD_LEFT: return 0xff51;
    case OHOS_KEY_DPAD_UP: return 0xff52;
    case OHOS_KEY_DPAD_RIGHT: return 0xff53;
    case OHOS_KEY_DPAD_DOWN: return 0xff54;
    case OHOS_KEY_PAGE_UP: return 0xff55;
    case OHOS_KEY_PAGE_DOWN: return 0xff56;
    case OHOS_KEY_MOVE_HOME: return 0xff50;
    case OHOS_KEY_MOVE_END: return 0xff57;
    default:
      if (code >= OHOS_KEY_F1 && code <= OHOS_KEY_F12)
	return 0xffbe + (code - OHOS_KEY_F1);
      return 0;
    }
}

static int
ohos_read_socket (struct terminal *terminal, struct input_event *hold_quit)
{
  unsigned key, modifiers;
  int count = 0;
  int mouse, button, mx, my, press;
  (void) hold_quit;

  while (ohos_pop_input (&key, &modifiers, &mouse, &button, &mx, &my,
			 &press))
    {
      struct input_event ev;
      int emods = 0;
      unsigned char_code = ohos_char_from_key (key);

      if (key & OHOS_INPUT_RESIZE_FLAG)
	{
	  int width = (int) (key & 0xffff);
	  int height = (int) modifiers;
	  Lisp_Object frame = selected_frame;

	  if (!NILP (frame) && FRAME_LIVE_P (XFRAME (frame)))
	    ohos_handle_surface_resize (XFRAME (frame), width, height);
	  count++;
	  continue;
	}

      if (mouse)
	{
	  struct ohos_display_info *dpyinfo = terminal->display_info.ohos;
	  if (dpyinfo)
	    {
	      dpyinfo->last_mouse_motion_x = mx;
	      dpyinfo->last_mouse_motion_y = my;
	    }
	  EVENT_INIT (ev);
	  ev.kind = MOUSE_CLICK_EVENT;
	  ev.code = button & 3;
	  ev.modifiers = press ? down_modifier : up_modifier;
	  if (modifiers & OHOS_MOD_CTRL)
	    ev.modifiers |= ctrl_modifier;
	  if (modifiers & OHOS_MOD_SHIFT)
	    ev.modifiers |= shift_modifier;
	  if (modifiers & OHOS_MOD_ALT)
	    ev.modifiers |= meta_modifier;
	  XSETINT (ev.x, mx);
	  XSETINT (ev.y, my);
	  ev.frame_or_window = selected_frame;
	  ev.arg = Qnil;
	  ev.timestamp = 0;
	  kbd_buffer_store_event (&ev);
	  count++;
	  continue;
	}

      EVENT_INIT (ev);
      ev.frame_or_window = selected_frame;
      ev.timestamp = 0;

      if (key & OHOS_INPUT_FOCUS_FLAG)
	{
	  struct ohos_display_info *dpyinfo = terminal->display_info.ohos;

	  ev.kind = modifiers ? FOCUS_IN_EVENT : FOCUS_OUT_EVENT;
	  ev.code = 0;
	  ev.modifiers = 0;
	  if (dpyinfo)
	    dpyinfo->focus_event_frame = modifiers ? SELECTED_FRAME () : NULL;
	  if (modifiers && SELECTED_FRAME ())
	    ohos_focus_frame (SELECTED_FRAME (), true);
	  kbd_buffer_store_event (&ev);
	  count++;
	  continue;
	}
      if (key & OHOS_INPUT_RAW_FLAG)
	{
	  ev.kind = ASCII_KEYSTROKE_EVENT;
	  ev.code = key & 0xff;
	  ev.modifiers = 0;
	  kbd_buffer_store_event (&ev);
	  count++;
	  continue;
	}
      if (key & OHOS_INPUT_TEXT_FLAG)
	{
	  unsigned cp = key & 0x1fffff;
	  ev.kind = cp < 0x80 ? ASCII_KEYSTROKE_EVENT
	    : MULTIBYTE_CHAR_KEYSTROKE_EVENT;
	  ev.code = cp;
	  ev.modifiers = 0;
	  kbd_buffer_store_event (&ev);
	  count++;
	  continue;
	}

      if (char_code)
	{
	  if (char_code >= 'a' && char_code <= 'z')
	    {
	      bool upper = ((modifiers & OHOS_MOD_SHIFT) != 0)
		^ ((modifiers & OHOS_MOD_CAPS) != 0);
	      if (upper)
		char_code -= 32;
	    }
	  else if (modifiers & OHOS_MOD_SHIFT)
	    {
	      static const char *from = "`1234567890-=[]\\;',./";
	      static const char *to = "~!@#$%^&*()_+{}|:\"<>?";
	      const char *p = strchr (from, char_code);
	      if (p)
		char_code = to[p - from];
	    }
	  if (modifiers & OHOS_MOD_CTRL)
	    char_code = make_ctrl_char (char_code);
	  if (modifiers & (OHOS_MOD_ALT | OHOS_MOD_META))
	    emods |= meta_modifier;

	  ev.kind = ASCII_KEYSTROKE_EVENT;
	  ev.code = char_code;
	}
      else
	{
	  unsigned keysym = ohos_keysym_from_key (key);
	  if (!keysym)
	    continue;
	  if (modifiers & OHOS_MOD_SHIFT)
	    emods |= shift_modifier;
	  if (modifiers & OHOS_MOD_CTRL)
	    emods |= ctrl_modifier;
	  if (modifiers & (OHOS_MOD_ALT | OHOS_MOD_META))
	    emods |= meta_modifier;
	  ev.kind = NON_ASCII_KEYSTROKE_EVENT;
	  ev.code = keysym;
	}
      ev.modifiers = emods;
      kbd_buffer_store_event (&ev);
      count++;
    }

  {
    int fd = ohos_input_wait_fd ();
    char buf[64];
    if (fd >= 0)
      while (read (fd, buf, sizeof buf) > 0)
	;
  }
  return count;
}

/* ------------------------------------------------------------------ */
/* Frame cursor handles.                                               */

void
ohos_init_frame_cursors (struct frame *f)
{
  struct ohos_output *o = FRAME_OUTPUT_DATA (f);

  o->text_cursor = (Emacs_Cursor) OHOS_CURSOR_TEXT;
  o->nontext_cursor = (Emacs_Cursor) OHOS_CURSOR_NONTEXT;
  o->modeline_cursor = (Emacs_Cursor) OHOS_CURSOR_MODELINE;
  o->hand_cursor = (Emacs_Cursor) OHOS_CURSOR_HAND;
  o->hourglass_cursor = (Emacs_Cursor) OHOS_CURSOR_HOURGLASS;
  o->horizontal_drag_cursor = (Emacs_Cursor) OHOS_CURSOR_HDRAG;
  o->vertical_drag_cursor = (Emacs_Cursor) OHOS_CURSOR_VDRAG;
  o->left_edge_cursor = (Emacs_Cursor) OHOS_CURSOR_LEFT_EDGE;
  o->top_left_corner_cursor = (Emacs_Cursor) OHOS_CURSOR_TOP_LEFT_CORNER;
  o->top_edge_cursor = (Emacs_Cursor) OHOS_CURSOR_TOP_EDGE;
  o->top_right_corner_cursor = (Emacs_Cursor) OHOS_CURSOR_TOP_RIGHT_CORNER;
  o->right_edge_cursor = (Emacs_Cursor) OHOS_CURSOR_RIGHT_EDGE;
  o->bottom_right_corner_cursor
    = (Emacs_Cursor) OHOS_CURSOR_BOTTOM_RIGHT_CORNER;
  o->bottom_edge_cursor = (Emacs_Cursor) OHOS_CURSOR_BOTTOM_EDGE;
  o->bottom_left_corner_cursor
    = (Emacs_Cursor) OHOS_CURSOR_BOTTOM_LEFT_CORNER;
  o->no_cursor = (Emacs_Cursor) OHOS_CURSOR_NONE;
  o->invisible_cursor = (Emacs_Cursor) OHOS_CURSOR_NONE;
  o->current_cursor = (Emacs_Cursor) OHOS_CURSOR_TEXT;
}

/* True when the native OHOS terminal exists, i.e. the host command
   channel has a consumer.  */

bool
ohos_gui_available (void)
{
  for (struct terminal *t = terminal_list; t; t = t->next_terminal)
    if (t->type == output_ohos)
      return true;
  return false;
}

/* ------------------------------------------------------------------ */
/* Terminal initialization.                                            */

static struct redisplay_interface ohos_redisplay_interface;

struct terminal *
ohos_create_terminal (struct ohos_display_info *dpyinfo)
{
  struct terminal *terminal = create_terminal (output_ohos,
                                                &ohos_redisplay_interface);
  terminal->display_info.ohos = dpyinfo;
  dpyinfo->terminal = terminal;
  terminal->kboard = allocate_kboard (Qohos);
  terminal->read_socket_hook = ohos_read_socket;
  terminal->delete_frame_hook = ohos_delete_frame;
  terminal->delete_terminal_hook = ohos_delete_terminal;
  terminal->clear_frame_hook = ohos_clear_frame;
  terminal->defined_color_hook = ohos_defined_color;
  terminal->query_frame_background_color
    = ohos_query_frame_background_color;
  terminal->query_colors = ohos_query_colors;

  /* Frame management.  */
  terminal->frame_visible_invisible_hook = ohos_frame_visible_invisible;
  terminal->fullscreen_hook = ohos_fullscreen_hook;
  terminal->iconify_frame_hook = ohos_iconify_frame;
  terminal->set_window_size_hook = ohos_set_window_size;
  terminal->set_window_size_and_position_hook
    = ohos_set_window_size_and_position;
  terminal->set_frame_offset_hook = ohos_set_offset;
  terminal->set_frame_alpha_hook = ohos_set_frame_alpha;
  terminal->set_new_font_hook = ohos_set_new_font;
  terminal->set_bitmap_icon_hook = NULL;
  terminal->implicit_set_name_hook = ohos_implicitly_set_name;
  terminal->frame_raise_lower_hook = ohos_frame_raise_lower;

  /* Focus and mouse.  */
  terminal->get_focus_frame = ohos_get_focus_frame;
  terminal->focus_frame_hook = ohos_focus_frame;
  terminal->frame_rehighlight_hook = ohos_frame_rehighlight_hook;
  terminal->mouse_position_hook = ohos_mouse_position;

  /* Menus and dialogs.  */
  terminal->menu_show_hook = ohos_menu_show;
  terminal->popup_dialog_hook = ohos_popup_dialog;
#ifdef HAVE_EXT_MENU_BAR
  terminal->activate_menubar_hook = ohos_activate_menubar;
#endif
  terminal->change_tab_bar_height_hook = ohos_change_tab_bar_height;
  terminal->change_tool_bar_height_hook = ohos_change_tool_bar_height;

  /* Scroll bars.  */
  terminal->set_vertical_scroll_bar_hook = ohos_set_vertical_scroll_bar;
  terminal->set_horizontal_scroll_bar_hook = ohos_set_horizontal_scroll_bar;
  terminal->set_scroll_bar_default_width_hook
    = ohos_set_scroll_bar_default_width;
  terminal->set_scroll_bar_default_height_hook
    = ohos_set_scroll_bar_default_height;
  terminal->condemn_scroll_bars_hook = ohos_condemn_scroll_bars;
  terminal->redeem_scroll_bar_hook = ohos_redeem_scroll_bar;
  terminal->judge_scroll_bars_hook = ohos_judge_scroll_bars;

  /* Update and cursor hooks.  */
  terminal->update_begin_hook = ohos_update_begin;
  terminal->update_end_hook = ohos_update_end;
  terminal->frame_up_to_date_hook = ohos_frame_up_to_date;
  terminal->buffer_flipping_unblocked_hook = ohos_buffer_flipping_unblocked;
  terminal->ring_bell_hook = ohos_ring_bell;
  terminal->toggle_invisible_pointer_hook = ohos_toggle_invisible_pointer;
  terminal->free_pixmap = ohos_free_pixmap;
  terminal->toolkit_position_hook = ohos_toolkit_position;
  terminal->any_grab_hook = ohos_any_grab;
  terminal->get_string_resource_hook = ohos_get_string_resource;

  return terminal;
}

static ohos_set_title_fn ohos_title_sink;
static ohos_set_clipboard_fn ohos_clipboard_sink;

void
ohos_set_title_sink (ohos_set_title_fn fn)
{
  ohos_title_sink = fn;
}

void
ohos_set_clipboard_sink (ohos_set_clipboard_fn fn)
{
  ohos_clipboard_sink = fn;
}

void
ohos_set_title (const char *title)
{
  if (ohos_title_sink)
    ohos_title_sink (title);
}

void
ohos_set_clipboard (const char *b64)
{
  if (ohos_clipboard_sink)
    ohos_clipboard_sink (b64);
}

struct redisplay_interface *
ohos_get_redisplay_interface (void)
{
  return &ohos_redisplay_interface;
}

static struct redisplay_interface ohos_redisplay_interface =
  {
    ohos_frame_parm_handlers,
    gui_produce_glyphs,
    gui_write_glyphs,
    gui_insert_glyphs,
    gui_clear_end_of_line,
    ohos_scroll_run,
    ohos_after_update_window_line,
    ohos_update_window_begin,
    ohos_update_window_end,
    ohos_flush,
    gui_clear_window_mouse_face,
    gui_get_glyph_overhangs,
    gui_fix_overlapping_area,
    ohos_draw_fringe_bitmap,
    ohos_define_fringe_bitmap,
    ohos_destroy_fringe_bitmap,
    ohos_compute_glyph_string_overhangs,
    ohos_draw_glyph_string,
    ohos_define_frame_cursor,
    ohos_clear_frame_area,
    ohos_clear_under_internal_border,
    ohos_draw_window_cursor,
    ohos_draw_vertical_window_border,
    ohos_draw_window_divider,
    ohos_shift_glyphs_for_insert,
    ohos_show_hourglass,
    ohos_hide_hourglass,
    ohos_default_font_parameter,
  };

void
syms_of_ohosgui (void)
{
  DEFSYM (Qohos, "ohos");
  DEFSYM (Qohos_scroll_bar_foreground, "scroll-bar-foreground");
  DEFSYM (Qohos_scroll_bar_background, "scroll-bar-background");
  ohos_frame_parm_init ();
  syms_of_ohosfns ();
  /* syms_of_ohosfont is called from emacs.c next to this function.  */
  syms_of_ohosselect ();
  syms_of_ohosmenu ();
}
