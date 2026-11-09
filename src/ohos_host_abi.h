/* Versioned ABI between GNU Emacs' OHOS backend (libemacs_gui.so) and the
   HarmonyOS host (libentry.so).

   There are exactly two directions:

     struct ohos_host_api   host -> Emacs: drawing and frame properties.
     struct ohos_emacs_api  Emacs -> host: lifecycle, input, draw pump,
                            host command channel.

   The host resolves the single exported symbol `ohos_emacs_api' and calls
   its `run' entry with the host API.  No other C symbol is part of the
   contract; both libraries hide everything else at link time.

   Host command channel (Emacs -> host, drained by take_command): one
   newline-free string per command.

     resize W H              move X Y               raise | lower
     minimize                fullscreen 0|1         window-visible 0|1
     no-focus-on-map 0|1     no-accept-focus 0|1    alpha PERCENT
     cursor NAME             hourglass 0|1          bell
     menubar JSON            menu-open ID TITLE JSON
     menu-part ID TOTAL LEN JSON
     menu-close ID
     clipboard-get ID
     battery ID
     pick-user-dir ID
     pointer-warp X Y

   Commands carrying an ID expect resolve_command (ID, INDEX, TEXT) as the
   answer.  The string payloads are NUL-terminated and must fit the
   maximum command size announced by the host.  */

#ifndef OHOS_HOST_ABI_H
#define OHOS_HOST_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OHOS_GUI_ABI_VERSION 2

/* Draw command stream shared by redisplay and the host.  The layout is
   part of the ABI; keep the app-side mirror in sync with the static
   assertions in gui/draw_protocol.h.  */
enum ohos_draw_opcode
{
  OHOS_OP_CLEAR = 1,
  OHOS_OP_FILL,
  OHOS_OP_TEXT,
  OHOS_OP_CURSOR,
  OHOS_OP_SCROLL,
  OHOS_OP_PRESENT,
  OHOS_OP_IMAGE,
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
  /* IMAGE op: RGBA pixels owned by the queue while it is being drained.  */
  const void *ptr;
  unsigned ptr_len;
};

struct ohos_host_api
{
  uint32_t abi_version;

  struct
  {
    void (*clear) (uint32_t bg);
    void (*fill) (int x, int y, int w, int h, uint32_t color);
    void (*text) (int x, int y, int w, int h, unsigned code,
		  uint32_t fg, uint32_t bg, uint32_t flags);
    void (*cursor) (int x, int y, int w, int h, int bar,
		    uint32_t fg, uint32_t bg, uint32_t flags, int kind);
    void (*scroll) (int x, int from_y, int width, int height, int to_y);
    void (*present) (void);
    /* Optional faster path: apply one batch of commands.  */
    void (*batch) (const struct ohos_draw_command *cmds, int n);
    /* Called once after the queue was drained, at most once per tick.  */
    void (*pump_done) (void);
    /* Called after a PRESENT was queued, to wake the UI thread.  */
    void (*wakeup) (void);
  } draw;

  struct
  {
    void (*metrics) (int width, int height, int cell_width, int cell_height,
		     int ascent, int descent);
    void (*title) (const char *title);
    /* Base64 payload produced by Emacs for the system pasteboard.  */
    void (*clipboard) (const char *base64);
    void (*log) (const char *line);
  } frame;
};

struct ohos_emacs_api
{
  uint32_t abi_version;

  /* Start Emacs on the calling thread.  STORES HOST for the lifetime of
     the process and returns Emacs' main return value.  */
  int (*run) (int argc, char **argv, const struct ohos_host_api *host);

  int (*input_key) (unsigned key, unsigned modifiers, int pressed);
  void (*input_text) (const char *text);
  void (*input_raw) (const char *data, size_t len);
  void (*input_mouse) (int button, int x, int y,
		       unsigned modifiers, int press);
  void (*input_wheel) (int delta, int col, int row, unsigned modifiers);
  void (*input_motion) (int x, int y, unsigned modifiers);
  void (*input_focus) (int focused);
  void (*input_resize) (int width, int height);
  void (*input_expose) (int x, int y, int width, int height);
  void (*font_metrics) (int ascent, int descent, int height,
			int space_width, int underline_position,
			int underline_thickness);

  int (*take_command) (char *buf, int cap);
  void (*resolve_command) (int id, int index, const char *text);

  void (*pump_draw) (void);
  int (*draw_pending) (void);
};

/* Returns NULL when HOST_ABI_VERSION does not match this build.  */
const struct ohos_emacs_api *ohos_emacs_api (uint32_t host_abi_version);

#ifdef __cplusplus
}
#endif

#endif /* OHOS_HOST_ABI_H */
