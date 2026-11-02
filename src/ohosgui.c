/* OHOS native GUI redisplay skeleton.
   This is the display-engine side of the native window backend: the
   redisplay_interface consumes Emacs glyph rows and forwards cells to
   the OHOS frame sink.  Frame/terminal creation is added in the next
   patch; this file deliberately has no tty/pty dependency.  */

#include <config.h>
#include <string.h>
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
#include "ohosgui.h"

static void
ohos_draw_glyph_string (struct glyph_string *s)
{
  static int calls;
  if ((calls++ % 200) == 0)
    ohos_log ("ohos_draw_glyph_string");
  if (!s || !s->f || !s->first_glyph || s->nchars <= 0 || !s->face)
    return;

  int line_height = FRAME_LINE_HEIGHT (s->f);
  if (line_height <= 0)
    return;

  int pixel_x = s->x;
  for (int i = 0; i < s->nchars; i++)
    {
      struct glyph *g = s->first_glyph + i;
      if (g->type != CHAR_GLYPH)
	continue;

      int width = g->pixel_width;
      if (width <= 0)
	width = FRAME_COLUMN_WIDTH (s->f);
      if (width <= 0)
	continue;

      uint32_t flags = (s->face->background != s->face->foreground) ? 1 : 0;
      if (s->face->underline != FACE_NO_UNDERLINE)
	flags |= 4;
      ohos_draw_text (pixel_x, s->y, width, line_height, g->u.ch,
		      (uint32_t) s->face->foreground,
		      (uint32_t) s->face->background, flags);
      pixel_x += width;
    }
}

static void
ohos_clear_frame_area (struct frame *f, int x, int y, int width, int height)
{
  ohos_draw_fill (x, y, width, height, (uint32_t) FRAME_BACKGROUND_PIXEL (f));
}

static void
ohos_clear_frame (struct frame *f)
{
  static int calls;
  if ((calls++ % 20) == 0)
    ohos_log ("ohos_clear_frame");
  ohos_draw_clear ((uint32_t) FRAME_BACKGROUND_PIXEL (f));
}

static bool ohos_force_redraw;

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
  static bool mouse_enabled;
  if (!mouse_enabled)
    {
      mouse_enabled = true;
      Lisp_Object function = intern ("xterm-mouse-mode");
      if (!NILP (Ffboundp (function)))
	{
	  Lisp_Object args[2] = { function, make_fixnum (1) };
	  Ffuncall (2, args);
	}
    }
  if (ohos_force_redraw)
    {
      ohos_force_redraw = false;
      ohos_draw_clear ((uint32_t) FRAME_BACKGROUND_PIXEL (f));
      SET_FRAME_GARBAGED (f);
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
  if (run)
    ohos_draw_scroll (run->current_y, run->desired_y, run->height);
  ohos_force_redraw = true;
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
  if (line_height <= 0)
    return;
  if (width <= 0)
    width = FRAME_COLUMN_WIDTH (f);
  int fx = WINDOW_TEXT_TO_FRAME_PIXEL_X (w, x);
  int fy = WINDOW_TOP_EDGE_Y (w) + y;
  ohos_draw_cursor (fx, fy, width, line_height,
		    (uint32_t) FRAME_FOREGROUND_PIXEL (f),
		    (uint32_t) FRAME_BACKGROUND_PIXEL (f),
		    on_p ? 1u : 0u);
}

static void
ohos_draw_vertical_window_border (struct window *w, int x, int y0, int y1)
{
  (void) w; (void) x; (void) y0; (void) y1;
}

static void
ohos_draw_window_divider (struct window *w, int x0, int x1, int y0, int y1)
{
  (void) w; (void) x0; (void) x1; (void) y0; (void) y1;
}

static void
ohos_clear_under_internal_border (struct frame *f)
{
  (void) f;
}

static void
ohos_define_frame_cursor (struct frame *f, Emacs_Cursor cursor)
{
  (void) f; (void) cursor;
}

static void
ohos_compute_glyph_string_overhangs (struct glyph_string *s)
{
  (void) s;
}

static void
ohos_define_fringe_bitmap (int which, unsigned short *bits, int h, int wd)
{
  (void) which; (void) bits; (void) h; (void) wd;
}

static void
ohos_destroy_fringe_bitmap (int which)
{
  (void) which;
}

static void
ohos_draw_fringe_bitmap (struct window *w, struct glyph_row *row,
                         struct draw_fringe_bitmap_params *p)
{
  (void) w; (void) row; (void) p;
}

int
XParseGeometry (const char *string, int *x, int *y, unsigned int *width,
                unsigned int *height)
{
  (void) string; (void) x; (void) y; (void) width; (void) height;
  return 0;
}
static struct redisplay_interface ohos_redisplay_interface;

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
  (void) terminal; (void) hold_quit;

  while (ohos_pop_input (&key, &modifiers))
    {
      struct input_event ev;
      int emods = 0;
      unsigned char_code = ohos_char_from_key (key);

      EVENT_INIT (ev);
      ev.frame_or_window = selected_frame;
      ev.timestamp = 0;

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

static void
ohos_delete_frame (struct frame *f)
{
  (void) f;
}

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
  terminal->clear_frame_hook = ohos_clear_frame;
  terminal->defined_color_hook = ohos_defined_color;
  terminal->query_frame_background_color
    = ohos_query_frame_background_color;
  terminal->get_focus_frame = NULL;
  return terminal;
}

struct redisplay_interface *
ohos_get_redisplay_interface (void)
{
  return &ohos_redisplay_interface;
}

static struct redisplay_interface ohos_redisplay_interface =
  {
    NULL, /* frame_parm_handlers */
    gui_produce_glyphs,
    gui_write_glyphs,
    gui_insert_glyphs,
    gui_clear_end_of_line,
    ohos_scroll_run,
    ohos_after_update_window_line,
    NULL,
    NULL,
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
    NULL,
    NULL,
    NULL,
    NULL,
  };


void
syms_of_ohosgui (void)
{
  DEFSYM (Qohos, "ohos");
  syms_of_ohosfns ();
}
