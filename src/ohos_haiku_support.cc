/* Minimal Haiku-compatible support layer for the HarmonyOS GUI spike.
   This is not a Haiku implementation: it translates the Haiku C support
   API used by haikuterm/haikufns into ANSI output on the existing pty.
   It lets the real Emacs redisplay_interface and font driver run without
   a Haiku toolkit.  */

#include <config.h>
#include "haiku_support.h"
#include "haikuselect.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cerrno>
#include <cmath>
#include <poll.h>
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>

extern "C" {
status_t wait_for_objects (object_wait_info *, int) { return B_TIMEOUT; }
status_t wait_for_objects_etc (object_wait_info *, int, uint32_t, bigtime_t) { return B_TIMEOUT; }
status_t write_port (port_id, int32_t, const void *, size_t) { return B_ERROR; }
ssize_t read_port (port_id, int32_t *, void *, size_t) { return B_ERROR; }
ssize_t port_buffer_size (port_id) { return B_ERROR; }
ssize_t port_buffer_size_etc (port_id, uint32_t, bigtime_t) { return B_ERROR; }
bigtime_t system_time (void) { struct timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts); return (bigtime_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000; }
port_id port_application_to_emacs = 0;
port_id port_popup_menu_to_emacs = 0;
port_id port_emacs_to_session_manager = 1;

struct ohos_font { int size; };
struct ohos_view;
struct ohos_window {
  int cols = 80, rows = 24;
  bool active = true;
  ohos_view *view = nullptr;
};

struct ohos_view {
  ohos_window *win = nullptr;
  int pen_x = 0, pen_y = 0;
  uint32_t fg = 0xffffff, bg = 0x000000;
  struct ohos_font *font = nullptr;
  std::string pending;
};

static ohos_window *active_window;

static void
ansi_color (uint32_t rgb, bool background)
{
  int r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
  int ri = r >= 128 ? 1 : 0, gi = g >= 128 ? 1 : 0, bi = b >= 128 ? 1 : 0;
  int code = 30 + ri + (gi << 1) + (bi << 2);
  if (background)
    code += 10;
  fprintf (stdout, "\033[%dm", code);
}

static void
emit_cursor (int x, int y)
{
  fprintf (stdout, "\033[%d;%dH", y + 1, x + 1);
}

static int
utf8_width (uint32_t cp)
{
  return ((cp >= 0x1100 && cp <= 0x115f) || (cp >= 0x2e80 && cp <= 0xa4cf)
          || (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff)
          || (cp >= 0xff00 && cp <= 0xff60) || (cp >= 0x1f300 && cp <= 0x1faff))
    ? 2 : 1;
}

static const char *
decode_utf8 (const char *p, uint32_t *cp)
{
  unsigned char c = (unsigned char) *p++;
  if (c < 0x80) { *cp = c; return p; }
  if ((c & 0xe0) == 0xc0) { *cp = ((c & 0x1f) << 6) | ((*p++) & 0x3f); return p; }
  if ((c & 0xf0) == 0xe0)
    { *cp = ((c & 0x0f) << 12) | (((unsigned char) p[0] & 0x3f) << 6)
        | ((unsigned char) p[1] & 0x3f); return p + 2; }
  *cp = ((c & 0x07) << 18) | (((unsigned char) p[0] & 0x3f) << 12)
    | (((unsigned char) p[1] & 0x3f) << 6) | ((unsigned char) p[2] & 0x3f);
  return p + 3;
}

void *
BWindow_new (void **view)
{
  ohos_window *w = new ohos_window;
  ohos_view *v = new ohos_view;
  w->view = v;
  v->win = w;
  *view = v;
  active_window = w;
  fprintf (stdout, "\033[2J\033[H\033[0m");
  return w;
}

void BWindow_quit (void *window) { delete (ohos_window *) window; }
void BWindow_set_offset (void *, int, int) {}
void BWindow_iconify (void *) {}
void BWindow_set_visible (void *, int) {}
void BWindow_retitle (void *, const char *title)
{
  if (title)
    fprintf (stdout, "\033]2;%s\007", title);
}
void BWindow_resize (void *window, int width, int height)
{
  ohos_window *w = (ohos_window *) window;
  w->cols = std::max (1, width / 9);
  w->rows = std::max (1, height / 18);
  fprintf (stdout, "\033[8;%d;%dt", w->rows, w->cols);
}
void BWindow_activate (void *) {}
void BWindow_center_on_screen (void *) {}
void BWindow_change_decoration (void *, int) {}
void BWindow_set_tooltip_decoration (void *) {}
void BWindow_set_avoid_focus (void *, int) {}
void BWindow_set_size_alignment (void *, int, int) {}
void BWindow_sync (void *) { fflush (stdout); }
void BWindow_send_behind (void *, void *) {}
bool BWindow_is_active (void *) { return true; }
void BWindow_set_override_redirect (void *, bool) {}
void BWindow_dimensions (void *window, int *width, int *height)
{
  ohos_window *w = (ohos_window *) window;
  *width = w->cols * 9;
  *height = w->rows * 18;
}
void BWindow_set_z_group (void *, enum haiku_z_group) {}
void BWindow_set_sticky (void *, bool) {}
void BWindow_Flush (void *) { fflush (stdout); }

void *BApplication_setup (void) { return (void *) 1; }
int be_get_display_planes (void) { return 24; }
void be_get_display_resolution (double *x, double *y) { *x = 96; *y = 96; }
int be_get_display_grayscale (void) { return 0; }
void *be_create_cursor_from_id (int id) { return (void *) (intptr_t) id; }
void be_set_cursor (void *) {}
void be_show_cursor (void) {}
void be_hide_cursor (void) {}
void be_reset_cursor (void) {}
void be_listen_font_settings (void) {}
int be_get_ui_color (const char *, uint32_t *color) { *color = 0xffffff; return 0; }
void be_roster_activate_app (const char *, const char *) {}

void BFont_close (void *font) { delete (ohos_font *) font; }
void BFont_metrics (void *, int *w, int *h, int *ascent, int *descent,
                    int *, int *, int *, int *, int *, int *)
{
  *w = 9; *h = 18; *ascent = 14; *descent = 4;
}
int BFont_have_char_p (void *, int32_t) { return 1; }
int BFont_have_char_block (void *, int32_t, int32_t) { return 1; }
void BFont_char_bounds (void *, const char *s, int *w, int *h, int *advance)
{
  uint32_t cp = 0;
  if (s && *s) decode_utf8 (s, &cp);
  *w = 9 * utf8_width (cp); *h = 18; *advance = *w;
}
void BFont_nchar_bounds (void *, const char *s, int *w, int *h, int *advance,
                         int32_t n)
{
  *w = 0;
  const char *p = s;
  for (int32_t i = 0; i < n && *p; i++)
    {
      uint32_t cp; p = decode_utf8 (p, &cp); *w += 9 * utf8_width (cp);
    }
  *h = 18; *advance = *w;
}
struct haiku_font_pattern *BFont_find (struct haiku_font_pattern *p) { return p; }
int BFont_open_pattern (struct haiku_font_pattern *, void **font, float) { *font = new ohos_font; return 0; }
void BFont_populate_fixed_family (struct haiku_font_pattern *) {}
void BFont_populate_plain_family (struct haiku_font_pattern *) {}

void BView_StartClip (void *) {}
void BView_EndClip (void *) {}
void BView_SetHighColor (void *view, uint32_t color)
{ ((ohos_view *) view)->fg = color; }
void BView_SetLowColor (void *view, uint32_t color)
{ ((ohos_view *) view)->bg = color; }
void BView_SetViewColor (void *view, uint32_t color)
{ ((ohos_view *) view)->bg = color; }
void BView_SetPenSize (void *, int) {}
void BView_SetFont (void *view, void *font)
{ ((ohos_view *) view)->font = (ohos_font *) font; }
void BView_MovePenTo (void *view, int x, int y)
{ ((ohos_view *) view)->pen_x = x; ((ohos_view *) view)->pen_y = y; }
void BView_DrawString (void *view, const char *text, ptrdiff_t len)
{
  ohos_view *v = (ohos_view *) view;
  const char *p = text, *end = text + len;
  ansi_color (v->fg, false);
  ansi_color (v->bg, true);
  while (p < end)
    {
      uint32_t cp;
      const char *next = decode_utf8 (p, &cp);
      if (cp == '\n') { v->pen_y += 18; v->pen_x = 0; p = next; continue; }
      emit_cursor (v->pen_x / 9, v->pen_y / 18);
      fwrite (p, 1, (size_t) (next - p), stdout);
      v->pen_x += 9 * utf8_width (cp);
      p = next;
    }
  fflush (stdout);
}
void BView_DrawChar (void *view, char c)
{ BView_DrawString (view, &c, 1); }
void BView_FillRectangle (void *view, int x, int y, int w, int h)
{
  ohos_view *v = (ohos_view *) view;
  ansi_color (v->bg, true);
  for (int yy = 0; yy < h; yy += 18)
    for (int xx = 0; xx < w; xx += 9)
      { emit_cursor ((x + xx) / 9, (y + yy) / 18); fputc (' ', stdout); }
  fflush (stdout);
}
void BView_FillRectangleAbs (void *v, int x, int y, int w, int h)
{ BView_FillRectangle (v, x, y, w, h); }
void BView_FillTriangle (void *, int, int, int, int, int, int) {}
void BView_StrokeRectangle (void *, int, int, int, int) {}
void BView_ClipToRect (void *, int, int, int, int) {}
void BView_ClipToInverseRect (void *, int, int, int, int) {}
void BView_StrokeLine (void *, int, int, int, int) {}
void BView_CopyBits (void *, int, int, int, int, int, int, int, int) {}
void BView_InvertRect (void *, int, int, int, int) {}
void BView_DrawBitmap (void *, void *, int, int, int, int, int, int, int, int, bool) {}
void BView_DrawBitmapWithEraseOp (void *, void *, int, int, int, int) {}
void BView_DrawBitmapTiled (void *, void *, int, int, int, int, int, int, int, int) {}
void BView_draw_lock (void *, bool, int, int, int, int) {}
void BView_draw_unlock (void *) {}
void BView_get_mouse (void *, int *x, int *y) { if (x) *x = 0; if (y) *y = 0; }
void BView_convert_to_screen (void *, int *x, int *y) { (void) x; (void) y; }
void BView_convert_from_screen (void *, int *x, int *y) { (void) x; (void) y; }
void BView_set_view_cursor (void *, void *) {}
void BView_set_tooltip (void *, const char *) {}
void BView_show_tooltip (void *) {}
void BView_publish_scroll_bar (void *, int, int, int, int) {}
void BView_forget_scroll_bar (void *, int, int, int, int) {}
void BView_scroll_bar_update (void *, int, int, int, int, bool) {}
bool BView_inside_scroll_bar (void *, int, int) { return false; }
void BView_emacs_delete (void *) {}
void be_delete_cursor (void *) {}
void haiku_font_pattern_free (struct haiku_font_pattern *) {}
void *be_make_scroll_bar_for_view (void *, int, int, int, int, int) { return nullptr; }
void BView_resize_to (void *, int, int) {}
void BView_move_frame (void *, int, int, int, int) {}
void BView_invalidate (void *) {}
void BView_invalidate_region (void *, int, int, int, int) {}
void BView_cr_dump_clipping (void *, void *) {}

int BScrollBar_default_size (int) { return 12; }
void BScrollBar_delete (void *) {}

void *BBitmap_new (int, int, int) { return nullptr; }
void *BBitmap_data (void *) { return nullptr; }
int BBitmap_convert (void *, void **) { return -1; }
void BBitmap_free (void *) {}
void BBitmap_dimensions (void *, int *w, int *h, int *a, int *b, int32_t *c, int *d)
{ *w = *h = 0; *a = *b = *c = *d = 0; }
void BBitmap_import_fringe_bitmap (void *, unsigned short *, int, int) {}
void be_draw_cross_on_pixmap (void *, int, int, int, int, uint32_t) {}

void *BAlert_new (const char *, enum haiku_alert_type) { return nullptr; }
void *BAlert_add_button (void *, const char *) { return nullptr; }
void BAlert_set_offset_spacing (void *) {}
int32 BAlert_go (void *, void (*) (void), void (*) (void), void (*) (void)) { return 0; }
void BAlert_delete (void *) {}
void BButton_set_enabled (void *, int) {}

void *BMenu_new_submenu (void *, const char *, bool) { return nullptr; }
void *BMenu_new_menu_bar_submenu (void *, const char *) { return nullptr; }
void *BMenuBar_new (void *) { return nullptr; }
void BMenuBar_delete (void *) {}
bool BMenuBar_start_tracking (void *) { return false; }
void BMenu_add_item (void *, const char *, void *, bool, bool, bool, void *,
                     const char *, const char *) {}
void BMenu_add_separator (void *) {}
int BMenu_count_items (void *) { return 0; }
void *BMenu_item_at (void *, int) { return nullptr; }
void *BMenu_item_get_menu (void *) { return nullptr; }
void BMenu_item_set_label (void *, const char *) {}
void BMenu_delete_all (void *) {}
void BMenu_delete_from (void *, int, int) {}
void *BPopUpMenu_new (const char *) { return nullptr; }
void BPopUpMenu_delete (void *) {}
void *BMenu_run (void *, int, int, void (*) (void *, void *),
                 void (*) (void), void (*) (void),
                 struct timespec (*) (void), void *) { return nullptr; }
void BMenu_add_title (void *, const char *) {}
void BMenu_set_font (void *, void *) {}
void *BMenu_find_item (void *, const char *) { return nullptr; }
void haiku_ring_bell (void) { fputc ('\a', stdout); fflush (stdout); }
void be_show_sticky_tooltip (void *, const char *, int, int) {}
void EmacsWindow_parent_to (void *, void *) {}
void EmacsWindow_unparent (void *) {}
void EmacsWindow_move_weak_child (void *, void *, int, int) {}
void be_get_version_string (char *buf, int size) { snprintf (buf, size, "OHOS"); }
int be_get_display_color_cells (void) { return 256; }
bool be_is_display_grayscale (void) { return false; }
void be_warp_pointer (int, int) {}
void EmacsView_set_up_double_buffering (void *) {}
void EmacsView_disable_double_buffering (void *) {}
void EmacsView_flip_and_blit (void *) {}
int EmacsView_double_buffered_p (void *) { return 0; }
char *be_popup_file_dialog (int, const char *, int, int, void *, const char *,
                            const char *, void (*) (void)) { return nullptr; }
size_t BBitmap_bytes_length (void *) { return 0; }

void *BMessage_new (void) { return nullptr; }
void BMessage_delete (void *) {}

void haiku_io_init (void) {}
void haiku_io_init_in_app_thread (void) {}
void haiku_read_size (ssize_t *size, bool)
{
  struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
  *size = poll (&pfd, 1, 0) > 0 ? (ssize_t) sizeof (struct haiku_key_event) : -1;
}
int haiku_read (enum haiku_event_type *type, void *buf, ssize_t size)
{
  unsigned char c;
  if (size < (ssize_t) sizeof (struct haiku_key_event) || read (STDIN_FILENO, &c, 1) != 1)
    return -1;
  struct haiku_key_event *ev = (struct haiku_key_event *) buf;
  memset (ev, 0, sizeof *ev);
  ev->window = active_window;
  ev->keysym = 0;
  ev->multibyte_char = c;
  *type = KEY_DOWN;
  return 0;
}
int haiku_read_with_timeout (enum haiku_event_type *type, void *buf, ssize_t size,
                             bigtime_t, bool)
{ return haiku_read (type, buf, size); }
int haiku_write (enum haiku_event_type, void *) { return 0; }
int haiku_write_without_signal (enum haiku_event_type, void *, bool) { return 0; }

void rgb_color_hsl (uint32_t, double *, double *, double *) {}
void hsl_color_rgb (double, double, double, uint32_t *) {}

}

extern "C" {
void be_apply_affine_transform (void *, double, double, double, double, double, double) {}
bool be_drag_and_drop_in_progress (void) { return false; }
void be_draw_image_mask (void *, void *, int, int, int, int, int, int, int, int, uint32_t) {}
void be_draw_bitmap_with_mask (void *, void *, void *, int, int, int, int, int, int, int, int, bool) {}
void be_evict_font_cache (void) {}
const char *be_find_setting (const char *name) { (void) name; return nullptr; }
int be_get_display_screens (void) { return 1; }
void be_get_screen_dimensions (int *w, int *h) { *w = 1920; *h = 1080; }
bool be_get_explicit_workarea (int *, int *, int *, int *) { return false; }
void be_get_window_decorator_dimensions (void *, int *l, int *t, int *r, int *b)
{ *l = *t = *r = *b = 0; }
void be_get_window_decorator_frame (void *, int *l, int *t, int *r, int *b)
{ *l = *t = *r = *b = 0; }
void be_handle_clipboard_changed_message (void) {}
void be_lock_window (void *) {}
void be_unlock_window (void *) {}
int be_plain_font_height (void) { return 18; }
int be_string_width_with_plain_font (const char *s)
{ int w = 0; while (s && *s) { w += 9; if ((*s & 0xc0) != 0x80) {} s++; } return w; }
void be_send_move_frame_event (void *) {}
void be_set_use_frame_synchronization (void *, bool) {}
void be_set_window_fullscreen_mode (void *, enum haiku_fullscreen_mode) {}
void debugger (const char *) {}
}

extern "C" {
void be_clear_grab_view (void) {}
void *be_create_pixmap_cursor (void *, int, int) { return nullptr; }
int be_enum_message (void *, int32 *type, int32, int32 *count, const char **name)
{ if (type) *type = 0; if (count) *count = 0; if (name) *name = ""; return 0; }
char *be_find_clipboard_data (enum haiku_clipboard, const char *, ssize_t *len)
{ if (len) *len = 0; return nullptr; }
int be_find_font_indices (struct haiku_font_pattern *, int *a, int *b)
{ if (a) *a = 0; if (b) *b = 0; return 0; }
void be_font_style_to_flags (const char *, struct haiku_font_pattern *) {}
int64 be_get_clipboard_count (enum haiku_clipboard) { return 0; }
const char *be_get_font_default (enum haiku_what_font) { return "Sans"; }
int be_get_font_size (enum haiku_what_font) { return 12; }
uint32 be_get_message_type (void *) { return 0; }
void be_init_font_data (void) {}
haiku_font_family_or_style *be_list_font_families (size_t *n)
{ if (n) *n = 0; return nullptr; }
int be_lock_clipboard_message (enum haiku_clipboard, void **msg, bool)
{ if (msg) *msg = nullptr; return -1; }
bool be_lock_font_defaults (void) { return true; }
void *be_open_font_at_index (int, int, float) { return new ohos_font; }
bool be_replay_menu_bar_event (void *, struct haiku_menu_bar_click_event *)
{ return false; }
bool be_select_font (void (*) (void), bool (*) (void),
                     haiku_font_family_or_style *, haiku_font_family_or_style *,
                     int *, bool, int, int, int, bool, bool *)
{ return false; }
void be_set_font_antialiasing (void *, bool) {}
void be_unlock_clipboard (enum haiku_clipboard, bool) {}
void be_unlock_font_defaults (void) {}
}

extern "C" {
int be_add_message_data (void *, const char *, int32, const void *, ssize_t) { return -1; }
int be_add_message_message (void *, const char *, void *) { return -1; }
int be_add_point_data (void *, const char *, float, float) { return -1; }
int be_add_refs_data (void *, const char *, const char *) { return -1; }
void be_clipboard_init (void) {}
bool be_clipboard_owner_p (enum haiku_clipboard) { return false; }
void *be_create_simple_message (void) { return nullptr; }
bool be_drag_message (void *, void *, bool, void (*) (void), void (*) (void),
                      void (*) (void), bool (*) (void)) { return false; }
int be_get_message_data (void *, const char *, int32, int32, const void **, ssize_t *)
{ return -1; }
void *be_get_message_message (void *, const char *, int32) { return nullptr; }
int be_get_point_data (void *, const char *, int32, float *, float *) { return -1; }
int be_get_refs_data (void *, const char *, int32, char **) { return -1; }
status_t be_roster_launch (const char *, const char *, char **, ptrdiff_t, void *, team_id *)
{ return B_ERROR; }
bool be_selection_outdated_p (enum haiku_clipboard, int64) { return false; }
void be_send_message (const char *, void *) {}
void be_set_clipboard_data (enum haiku_clipboard, const char *, const char *, ssize_t, bool) {}
void be_set_message_type (void *, uint32) {}
void be_start_watching_selection (enum haiku_clipboard) {}
void be_update_clipboard_count (enum haiku_clipboard) {}
status_t be_write_node_message (const char *, const char *, void *) { return B_ERROR; }
}

extern "C" intmax_t
be_display_notification (const char *, const char *, intmax_t, int, const char *)
{ return 0; }
