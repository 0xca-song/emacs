#ifndef OHOS_GUI_H
#define OHOS_GUI_H
#include <stdint.h>
#include "lisp.h"
#include "ohos_host_abi.h"
#include "ohos_render.h"
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
/* A pixmap is also the x_image (cairo/haiku model): the pixel buffer is
   the image, so image.c can read and write it directly through
   GET_PIXEL/PUT_PIXEL and nothing has to be uploaded separately.  */
struct ohos_pixmap
{
  int width, height, depth, bytes_per_line, bits_per_pixel;
  char *data;			/* ARGB32, row-major, `bytes_per_line' each */
};

extern struct ohos_pixmap *ohos_create_pixmap (int, int, int);
extern void ohos_free_image_pixmap (struct frame *, void *);
extern void ohos_put_pixel (struct ohos_pixmap *, int, int, unsigned long);
extern unsigned long ohos_get_pixel (struct ohos_pixmap *, int, int);

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
  double font_resolution;
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
  /* Stable cross-thread key of this frame, and the per-frame pixel
     backend.  Both are created in ohos_create_frame and released in
     ohos_delete_frame.  */
  uint32_t frame_id;
  struct ohos_renderer *renderer;
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
  bool frame_open;		/* begin_frame accepted this pass */
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

void ohos_log (const char *s);
/* Installed once by ohos_abi.c before Emacs starts.  */
void ohos_set_host_api (const struct ohos_host_api *api);
const struct ohos_host_api *ohos_get_host_api (void);

/* Per-frame window title.  */
void ohos_set_title (struct frame *f, const char *title);

/* ------------------------------------------------------------------ */
/* Typed host (ArkUI) channel.  All operations carry frame_id and are
   copied into the host's typed event mailbox; no command strings.  */

#define OHOS_HOST_RESULT_TEXT 65536

void ohos_host_result (int id, int index, const char *text);
int ohos_host_wait_result (int id, int timeout_ms, int *index,
			   char *text, int cap);
void ohos_host_menu_result (int id, int index);
void ohos_host_request_result (int id, int status, const char *text,
			       size_t len);
int ohos_menu_wait_result (int id, int timeout_ms);

void ohos_host_set_cursor (uint32_t frame_id, int cursor);
void ohos_host_set_hourglass (uint32_t frame_id, int on);
void ohos_host_bell (uint32_t frame_id);
void ohos_host_set_alpha (uint32_t frame_id, int percent);
void ohos_host_lower_window (uint32_t frame_id);
void ohos_host_set_visible (uint32_t frame_id, int visible);
void ohos_host_set_fullscreen (uint32_t frame_id, int fullscreen);
void ohos_host_minimize_window (uint32_t frame_id);
void ohos_host_pointer_warp (uint32_t frame_id, int x, int y);
void ohos_host_set_no_focus_on_map (uint32_t frame_id, int no_focus);
void ohos_host_set_no_accept_focus (uint32_t frame_id, int no_accept);
int ohos_host_menu_bar (uint32_t frame_id, const char *text, size_t len);
int ohos_host_menu_show (uint32_t frame_id, int menu_id, int flags,
			 int dialog, const char *text, size_t len);
void ohos_host_menu_close (uint32_t frame_id, int menu_id);
int ohos_host_request (uint32_t frame_id, int kind, int request_id,
		       const char *text, size_t len);

/* True when a native OHOS terminal is up and the host channel can be
   used.  */
bool ohos_gui_available (void);
void ohos_set_display_metrics (double density_dpi, double density_pixels,
			       double scaled_density, double xdpi, double ydpi);
int ohos_default_font_pixel_size (void);
double ohos_display_xdpi (void);
double ohos_display_ydpi (void);
double ohos_display_font_resolution (void);
void ohos_font_metrics (int ascent, int descent, int height,
			int space_width, int underline_position,
			int underline_thickness);
void ohos_set_clipboard (const char *b64);

/* ------------------------------------------------------------------ */
/* Per-frame pixel backend.

   Redisplay binds every drawing operation to its frame; there is no
   process-global renderer, queue or command protocol.  */

struct ohos_renderer *ohos_frame_renderer (struct frame *f);

/* Frame geometry published to the host and to the frame renderer.  */
void ohos_publish_frame_metrics (struct frame *f);
/* Called once the first redisplay of a frame finished.  */
void ohos_menubar_mark_ready (void);
bool ohos_menubar_ready_p (void);

void ohos_initialize_frame_menubar (struct frame *f);

/* UI thread -> Emacs input queue.  Every event names the frame it was
   produced for; the Emacs thread routes it independently of the global
   selected frame.  */
#define OHOS_INPUT_TEXT_FLAG 0x80000000u
#define OHOS_INPUT_RAW_FLAG  0x40000000u
#define OHOS_INPUT_FOCUS_FLAG 0x20000000u
#define OHOS_INPUT_RESIZE_FLAG 0x10000000u
#define OHOS_INPUT_EXPOSE_FLAG 0x08000000u
#define OHOS_INPUT_MENUBAR_FLAG 0x04000000u
#define OHOS_INPUT_CLOSE_FLAG 0x02000000u
#define OHOS_INPUT_WINDOW_FAILED_FLAG 0x01000000u
void ohos_create_input_pipe (void);
void ohos_input_raw (uint32_t frame_id, const char *data, size_t len);
void ohos_input_menubar (uint32_t frame_id, int index);
int ohos_input_wait_fd (void);
int ohos_input_key (uint32_t frame_id, unsigned key, unsigned modifiers,
		    int pressed);
uint64_t ohos_probe_pop_ns (void);
bool ohos_probe_pending (void);
void ohos_probe_log (const char *tag);
int ohos_pop_input (uint32_t *frame_id, unsigned *key, unsigned *modifiers,
		    int *mouse, int *button, int *x, int *y, int *press,
		    int *motion, int *wheel);
void ohos_input_wheel (uint32_t frame_id, int delta, int col, int row,
		       unsigned modifiers);
void ohos_input_mouse (uint32_t frame_id, int button, int x, int y,
		       unsigned modifiers, int press);
/* Pointer motion only: drives note_mouse_highlight (mouse-face, hand
   pointer) and `frame->mouse_moved'.  Coordinates are frame pixels.  */
void ohos_input_motion (uint32_t frame_id, int x, int y,
			unsigned modifiers);
void ohos_input_text (uint32_t frame_id, const char *text);
void ohos_input_focus (uint32_t frame_id, int focused);
void ohos_input_close_request (uint32_t frame_id);
void ohos_input_window_create_failed (uint32_t frame_id,
				      const char *reason);
/* UI thread: the XComponent surface now has WIDTH x HEIGHT pixels.
   Emacs adopts that geometry so the native frame fills the surface.  */
void ohos_input_resize (uint32_t frame_id, int width, int height);
void ohos_last_surface_size (int *width, int *height);
bool ohos_wait_surface_size (int timeout_ms);
/* Host thread: the surface was rebuilt or re-exposed, so the frame must
   be repainted.  Emacs marks the frame garbaged and redisplays.  */
void ohos_input_expose (uint32_t frame_id, int x, int y, int width,
			int height);
void ohos_apply_surface_events (void);
void ohos_note_frame_ready (void);
void ohos_surface_created (uint32_t frame_id, void *window, int width,
			   int height, uint64_t generation);
void ohos_surface_resized (uint32_t frame_id, int width, int height,
			   uint64_t generation);
void ohos_surface_destroyed (uint32_t frame_id, uint64_t generation);

/* Frame registry: frame_id -> frame/renderer, maintained by ohos.c.  */
uint32_t ohos_frame_register (struct frame *f,
			      struct ohos_renderer *renderer);
void ohos_frame_unregister (uint32_t frame_id);
struct frame *ohos_frame_by_id (uint32_t frame_id);
struct ohos_renderer *ohos_renderer_by_frame_id (uint32_t frame_id);

#ifdef __cplusplus
}
#endif
#endif
