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
  struct frame *focused_frame;
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

struct ohos_output
{
  struct ohos_display_info *display_info;
  struct font *font;
  int fontset;
  int width, height;
  void *parent_desc;
  uintptr_t root_window;
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
extern struct frame *ohos_create_frame (struct terminal *);
extern struct terminal *ohos_create_terminal (struct ohos_display_info *);
extern struct redisplay_interface *ohos_get_redisplay_interface (void);
extern void syms_of_ohosgui (void);
extern void syms_of_ohosfns (void);
extern void syms_of_ohosfont (void);
extern struct font *ohosfont_make_default (struct frame *);

typedef void (*ohos_frame_sink_fn) (int x, int y, int width, unsigned code,
                                    uint32_t fg, uint32_t bg,
                                    int with_background);
typedef void (*ohos_log_fn) (const char *);
void ohos_set_log_sink (ohos_log_fn fn);
void ohos_log (const char *s);
void ohos_set_frame_sink (ohos_frame_sink_fn fn);
void ohos_frame_text (int x, int y, int width, unsigned code,
                      uint32_t fg, uint32_t bg, int with_background);

#ifdef __cplusplus
}
#endif
#endif
