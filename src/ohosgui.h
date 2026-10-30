#ifndef OHOS_GUI_H
#define OHOS_GUI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Native OHOS frame output.  The first implementation is cell based:
   the redisplay interface emits glyphs into a shared command buffer and
   ArkTS/OH_Drawing presents them.  */
struct ohos_display_info
{
  struct terminal *terminal;
  Lisp_Object name_list_element;
  struct frame *focused_frame;
  Mouse_HLInfo mouse_highlight;
  struct frame *last_mouse_frame;
  double resx, resy;
  int n_planes;
  int color_p;
  int smallest_font_height;
  int smallest_char_width;
  Lisp_Object color_map;
  struct ohos_display_info *next;
};

struct ohos_output
{
  struct ohos_display_info *display_info;
  struct font *font;
  int fontset;
  int width, height;
  Emacs_Cursor current_cursor;
};

#define FRAME_DISPLAY_INFO(f) ((f)->output_data.ohos->display_info)
#define FRAME_FONT(f) ((f)->output_data.ohos->font)
#define FRAME_FONTSET(f) ((f)->output_data.ohos->fontset)

extern struct ohos_display_info *ohos_term_init (void);
extern struct frame *ohos_create_frame (struct terminal *);
extern struct redisplay_interface *ohos_get_redisplay_interface (void);
extern void syms_of_ohosgui (void);
extern void syms_of_ohosfont (void);

void ohos_frame_text (int x, int y, int width, unsigned code,
                      uint32_t fg, uint32_t bg, int with_background);

#ifdef __cplusplus
}
#endif
#endif
