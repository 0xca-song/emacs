#ifndef OHOS_GUI_H
#define OHOS_GUI_H
#include <stdint.h>
#include "lisp.h"
#include "frame.h"
#include "termhooks.h"
#include "dispextern.h"

typedef void *Emacs_Pix_Container;
typedef void *Emacs_Cursor;
typedef void *Emacs_Pixmap;
typedef unsigned long RGB_PIXEL_COLOR;
typedef struct ohos_display_info Display_Info;
#ifdef __cplusplus
extern "C" {
#endif

/* Native OHOS frame output.  The first implementation is cell based:
   the redisplay interface emits glyphs into a shared command buffer and
   ArkTS/OH_Drawing presents them.  */
struct ohos_bitmap_record
{
  void *img;
  char *file;
  int refcount;
  int height, width, depth;
  uint32_t stipple_foreground, stipple_background;
  void *stipple_bits;
};

struct ohos_display_info
{
  struct terminal *terminal;
  Lisp_Object name_list_element;
  /* Frame that currently has the input focus, and the frame the last
     focus event named.  */
  Lisp_Object focus_frame;
  struct frame *focus_event_frame;
  Mouse_HLInfo mouse_highlight;
  struct frame *last_mouse_frame;
  struct frame *last_mouse_motion_frame;
  struct frame *last_mouse_glyph_frame;
  struct frame *highlight_frame;
  int last_mouse_motion_x, last_mouse_motion_y;
  NativeRectangle last_mouse_glyph;
  Lisp_Object rdb;
  Lisp_Object default_name;
  int grabbed;
  double resx, resy;
  int n_fonts;
  struct ohos_bitmap_record *bitmaps;
  ptrdiff_t bitmaps_size;
  ptrdiff_t bitmaps_last;
  int n_planes;
  int color_p;
  int smallest_font_height;
  int smallest_char_width;
  uintptr_t root_window;
  Lisp_Object color_map;
  struct ohos_display_info *next;
};

/* One drawn scroll bar.  Scroll bars are painted with the normal draw
   command stream rather than as toolkit widgets, so the rectangle and
   handle position are kept here for hit testing and redraws.  */
struct ohos_scroll_bar
{
  struct ohos_scroll_bar *next;
  struct window *window;
  int x, y, width, height;	/* frame pixel rectangle */
  int portion, whole, position;
  bool horizontal;
  bool condemned;
};

struct ohos_output
{
  struct ohos_display_info *display_info;
  struct font *font;
  int fontset;
  int width, height;
  uintptr_t parent_desc;
  uintptr_t root_window;
  /* Colors decoded from frame parameters.  The draw pipeline reads the
     frame pixel colors, so these only need to be remembered when they
     differ from the frame defaults (cursor/mouse).  */
  unsigned long cursor_pixel;
  unsigned long cursor_foreground_pixel;
  unsigned long mouse_pixel;
  /* Window state mirrored from frame parameters, used by the host
     command channel to avoid redundant window operations.  */
  bool fullscreen_hint;
  int alpha_hint;
  int iconified;
  bool window_shown;
  /* Incremented whenever the frame geometry changes, so the host can
     tell a resize that came from Emacs from one the user performed.  */
  unsigned geometry_serial;
  /* Pixel size last reported by the ArkUI surface.  A resize request
     that matches this size came from the host itself and must not be
     echoed back as a window operation.  */
  int host_surface_width;
  int host_surface_height;
  bool surface_seen;		/* the host reported its surface size */
  /* Redisplay / interaction state.  */
  bool complete_p;		/* frame fully drawn in this update */
  bool update_in_progress;
  bool need_flip;		/* END queued but present deferred */
  unsigned epoch;		/* current redisplay pass number */
  bool hourglass;		/* busy indicator active */
  /* Cursor handles, see ohos_init_frame_cursors.  */
  Emacs_Cursor invisible_cursor;
  /* Scroll bar areas currently drawn, for hit testing and cleanup.  */
  struct ohos_scroll_bar *scroll_bars;
  Emacs_Cursor text_cursor;
  Emacs_Cursor nontext_cursor;
  Emacs_Cursor modeline_cursor;
  Emacs_Cursor hand_cursor;
  Emacs_Cursor hourglass_cursor;
  Emacs_Cursor horizontal_drag_cursor;
  Emacs_Cursor vertical_drag_cursor;
  Emacs_Cursor left_edge_cursor;
  Emacs_Cursor top_left_corner_cursor;
  Emacs_Cursor top_edge_cursor;
  Emacs_Cursor top_right_corner_cursor;
  Emacs_Cursor right_edge_cursor;
  Emacs_Cursor bottom_right_corner_cursor;
  Emacs_Cursor bottom_edge_cursor;
  Emacs_Cursor bottom_left_corner_cursor;
  Emacs_Cursor no_cursor;
  Emacs_Cursor current_cursor;
};

#ifndef XNegative
#define XNegative (1 << 4)
#define YNegative (1 << 5)
#define XValue (1 << 0)
#define YValue (1 << 1)
#define WidthValue (1 << 2)
#define HeightValue (1 << 3)
#define GCGraphicsExposures 0
#define NorthWestGravity 1
#define NorthGravity 2
#define NorthEastGravity 3
#define WestGravity 4
#define CenterGravity 5
#define EastGravity 6
#define SouthWestGravity 7
#define SouthGravity 8
#define SouthEastGravity 9
#define StaticGravity 10
#define USPosition (1 << 0)
#define USSize (1 << 1)
#define PPosition (1 << 2)
#define PSize (1 << 3)
#define PWinGravity (1 << 9)
#endif
extern int XParseGeometry (const char *, int *, int *, unsigned int *, unsigned int *);

#define FRAME_NATIVE_WINDOW(f) 0
#define FRAME_BASELINE_OFFSET(f) 0
#define BLACK_PIX_DEFAULT(f) 0x000000
#define WHITE_PIX_DEFAULT(f) 0xffffff
#define FRAME_OUTPUT_DATA(f) ((f)->output_data.ohos)
#define FRAME_DISPLAY_INFO(f) ((f)->output_data.ohos->display_info)
#define FRAME_FONT(f) ((f)->output_data.ohos->font)
#define FRAME_FONTSET(f) ((f)->output_data.ohos->fontset)

extern Lisp_Object Fxw_display_color_p (Lisp_Object);
extern Lisp_Object Fx_display_grayscale_p (Lisp_Object);
extern struct ohos_display_info *ohos_term_init (void);
extern struct frame *ohos_create_frame (struct terminal *, Lisp_Object);
extern struct terminal *ohos_create_terminal (struct ohos_display_info *);
extern struct redisplay_interface *ohos_get_redisplay_interface (void);
extern void syms_of_ohosgui (void);
extern void syms_of_ohosfns (void);
extern void ohos_frame_parm_init (void);
extern frame_parm_handler ohos_frame_parm_handlers[];
extern void ohos_set_window_size (struct frame *, bool, int, int);
extern void ohos_set_window_size_and_position (struct frame *, int, int);
extern void ohos_set_offset (struct frame *, int, int, int);
extern void ohos_set_new_font_object (void);
extern Lisp_Object ohos_set_new_font (struct frame *, Lisp_Object, int);
extern void ohos_implicitly_set_name (struct frame *, Lisp_Object,
				      Lisp_Object);
extern void ohos_fullscreen_hook (struct frame *);
extern void ohos_frame_visible_invisible (struct frame *, bool);
extern void ohos_iconify_frame (struct frame *);
extern void ohos_handle_surface_resize (struct frame *, int, int);
extern void ohos_change_tab_bar_height (struct frame *, int);
extern void ohos_change_tool_bar_height (struct frame *, int);
extern void syms_of_ohosfont (void);
extern void syms_of_ohosmenu (void);
extern void syms_of_ohosselect (void);
extern void ohos_default_font_parameter (struct frame *, Lisp_Object);
extern void ohos_init_frame_cursors (struct frame *);
extern Lisp_Object ohos_menu_show (struct frame *, int, int, int,
				   Lisp_Object, const char **);
extern Lisp_Object ohos_popup_dialog (struct frame *, Lisp_Object,
				      Lisp_Object);
extern void ohos_activate_menubar (struct frame *);
extern Lisp_Object ohos_get_selection_value (Lisp_Object, Lisp_Object,
					     bool, Lisp_Object);

/* Shared-GUI helpers implemented in ohosgui_stubs.c.  */
extern Lisp_Object Fxw_display_color_p (Lisp_Object);
extern Lisp_Object Fx_display_grayscale_p (Lisp_Object);
extern void image_sync_to_pixmaps (struct frame *, struct image *);
extern char *get_keysym_name (int);
extern struct font *ohosfont_make_default (struct frame *);
extern Lisp_Object ohosfont_make_default_object (struct frame *);
extern void ohosfont_register_frame (struct frame *);
extern Lisp_Object ohos_color_table;
extern void ohos_set_color_table (Lisp_Object);
extern void mark_ohosterm (void);
extern bool ohos_defined_color (struct frame *, const char *, Emacs_Color *,
				bool, bool);
extern void ohos_query_frame_background_color (struct frame *, Emacs_Color *);

typedef void (*ohos_frame_sink_fn) (int x, int y, int width, unsigned code,
                                    uint32_t fg, uint32_t bg,
                                    int with_background);
typedef void (*ohos_log_fn) (const char *);
typedef void (*ohos_clear_fn) (void);
void ohos_set_log_sink (ohos_log_fn fn);
void ohos_log (const char *s);
void ohos_set_frame_sink (ohos_frame_sink_fn fn);
void ohos_frame_text (int x, int y, int width, unsigned code,
                      uint32_t fg, uint32_t bg, int with_background);
void ohos_set_clear_sink (ohos_clear_fn fn);
void ohos_frame_clear (void);

/* Native draw command queue.  Emacs redisplay only enqueues commands;
   the ArkUI host thread drains them with ohos_pump_draw_commands.  */
typedef void (*ohos_draw_clear_fn) (uint32_t bg);
typedef void (*ohos_draw_fill_fn) (int x, int y, int w, int h,
				   uint32_t color);
typedef void (*ohos_draw_text_fn) (int x, int y, int w, int h,
				   unsigned code, uint32_t fg,
				   uint32_t bg, uint32_t flags);
typedef void (*ohos_draw_cursor_fn) (int x, int y, int w, int h,
				     uint32_t fg, uint32_t bg,
				     uint32_t flags);
typedef void (*ohos_draw_scroll_fn) (int x, int from_y, int width,
				     int height, int to_y);
typedef void (*ohos_draw_present_fn) (void);

void ohos_set_draw_clear_sink (ohos_draw_clear_fn fn);
void ohos_set_draw_fill_sink (ohos_draw_fill_fn fn);
void ohos_set_draw_text_sink (ohos_draw_text_fn fn);
void ohos_set_draw_cursor_sink (ohos_draw_cursor_fn fn);
void ohos_set_draw_scroll_sink (ohos_draw_scroll_fn fn);
void ohos_set_draw_present_sink (ohos_draw_present_fn fn);
/* Native frame property sinks: the host (ArkUI page) installs these to
   apply window title/clipboard changes.  */
typedef void (*ohos_set_title_fn) (const char *);
typedef void (*ohos_set_clipboard_fn) (const char *);
void ohos_set_title_sink (ohos_set_title_fn fn);
void ohos_set_clipboard_sink (ohos_set_clipboard_fn fn);
void ohos_set_title (const char *title);

/* ------------------------------------------------------------------ */
/* Host (ArkUI) chrome channel.

   Window operations that only ArkTS can perform (move/resize the native
   window, change its decoration, show a menu panel, post a
   notification) are queued as short text commands.  The ArkUI page
   drains the queue from its UI thread with `ohos_host_take_command',
   applies the command, and reports interactive results back with
   `ohos_host_menu_result'.  */

#define OHOS_HOST_CMD_MAX 8192
#define OHOS_HOST_RESULT_TEXT 65536

void ohos_host_command (const char *fmt, ...)
  ATTRIBUTE_FORMAT_PRINTF (1, 2);
int ohos_host_take_command (char *buf, int cap);

/* Request/response pair for operations that must return a value to
   Emacs (a menu selection, the clipboard contents).  Emacs allocates a
   small integer ID, queues the request, then waits for the host to call
   `ohos_host_result'.  */
void ohos_host_result (int id, int index, const char *text);
int ohos_host_wait_result (int id, int timeout_ms, int *index,
			   char *text, int cap);

/* Convenience wrappers used by the menu and selection backends.  */
void ohos_host_menu_result (int id, int index);
int ohos_menu_wait_result (int id, int timeout_ms);

/* Drain any queued chrome command.  Used by the input pump so a busy
   loop still lets window operations reach the host.  */
void ohos_host_drain (void);

/* True when a native OHOS terminal is up and the host channel can be
   used.  */
bool ohos_gui_available (void);
void ohos_font_metrics (int ascent, int descent, int height,
			int space_width, int underline_position,
			int underline_thickness);
void ohos_set_clipboard (const char *b64);

/* ------------------------------------------------------------------ */
/* Draw command stream shared with the ArkUI host.

   Emacs redisplay only enqueues commands; the host thread drains them in
   batches so it can apply a whole batch with a single lock.  BEGIN/END
   bracket one redisplay pass so the host can detect lost commands (the
   epoch sequence has a gap) and ask for a full repaint.  */

enum ohos_draw_opcode
{
  OHOS_OP_CLEAR = 1,
  OHOS_OP_FILL,
  OHOS_OP_TEXT,
  OHOS_OP_CURSOR,
  OHOS_OP_SCROLL,
  OHOS_OP_PRESENT,
  OHOS_OP_BEGIN,
  OHOS_OP_END
};

struct ohos_draw_command
{
  int op;
  int x, y, w, h;
  int to_y;
  unsigned code;
  uint32_t fg, bg;
  uint32_t flags;
  unsigned epoch;
};

typedef void (*ohos_draw_batch_fn) (const struct ohos_draw_command *, int);

void ohos_set_draw_batch_sink (ohos_draw_batch_fn fn);

/* Called once after the pump drained the queue, so the host can present
   at most once per tick instead of once per batch.  */
typedef void (*ohos_pump_done_fn) (void);
void ohos_set_pump_done_sink (ohos_pump_done_fn fn);
unsigned ohos_draw_epoch (void);
unsigned long ohos_draw_dropped_count (void);
void ohos_draw_begin (unsigned epoch);
void ohos_draw_end (unsigned epoch);

/* Frame geometry published to the host so it can allocate a bitmap of
   exactly the frame pixels instead of guessing from a cell grid.  */
typedef void (*ohos_frame_metrics_fn) (int width, int height, int cell_width,
				       int cell_height, int ascent,
				       int descent);
void ohos_set_frame_metrics_sink (ohos_frame_metrics_fn fn);
void ohos_publish_frame_metrics (struct frame *f);
/* Called once the first redisplay of a frame finished.  */
void ohos_menubar_mark_ready (void);
bool ohos_menubar_ready_p (void);

void ohos_initialize_frame_menubar (struct frame *f);

void ohos_draw_clear (uint32_t bg);
void ohos_draw_fill (int x, int y, int w, int h, uint32_t color);
void ohos_draw_text (int x, int y, int w, int h, unsigned code,
		     uint32_t fg, uint32_t bg, uint32_t flags);
void ohos_draw_cursor (int x, int y, int w, int h,
		       uint32_t fg, uint32_t bg, uint32_t flags);
void ohos_draw_scroll (int x, int from_y, int width, int height,
			 int to_y);
void ohos_draw_present (void);
void ohos_pump_draw_commands (void);

/* UI thread -> Emacs input queue.  */
#define OHOS_INPUT_TEXT_FLAG 0x80000000u
#define OHOS_INPUT_RAW_FLAG  0x40000000u
#define OHOS_INPUT_FOCUS_FLAG 0x20000000u
#define OHOS_INPUT_RESIZE_FLAG 0x10000000u
#define OHOS_INPUT_EXPOSE_FLAG 0x08000000u
void ohos_create_input_pipe (void);
void ohos_input_raw (const char *data, size_t len);
int ohos_input_wait_fd (void);
int ohos_input_key (unsigned key, unsigned modifiers, int pressed);
int ohos_pop_input (unsigned *key, unsigned *modifiers, int *mouse,
		    int *button, int *x, int *y, int *press, int *motion);
void ohos_input_mouse (int button, int x, int y, unsigned modifiers,
		       int press);
/* Pointer motion only: drives note_mouse_highlight (mouse-face, hand
   pointer) and `frame->mouse_moved'.  Coordinates are frame pixels.  */
void ohos_input_motion (int x, int y, unsigned modifiers);
void ohos_input_text (const char *text);
void ohos_input_focus (int focused);
/* UI thread: the XComponent surface now has WIDTH x HEIGHT pixels.
   Emacs adopts that geometry so the native frame fills the surface.  */
void ohos_input_resize (int width, int height);
/* Host thread: the surface was rebuilt or re-exposed, so the frame must
   be repainted.  Emacs marks the frame garbaged and redisplays.  */
void ohos_input_expose (int x, int y, int width, int height);

#ifdef __cplusplus
}
#endif
#endif
