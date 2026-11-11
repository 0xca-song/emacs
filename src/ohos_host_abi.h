/* Versioned ABI between GNU Emacs' OHOS backend (libemacs_gui.so) and the
   HarmonyOS host (libentry.so).

   There are exactly two directions:

     struct ohos_emacs_api  host -> Emacs: lifecycle, input, surface
                            events and command channel.
     struct ohos_host_api   Emacs -> host: frame properties, logging,
                            clipboard and wakeups.  Drawing does not cross
                            this ABI; it goes to the statically linked
                            renderer on the Emacs thread.

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

#define OHOS_GUI_ABI_VERSION 10

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
  OHOS_OP_IMAGE
};

struct ohos_draw_command
{
  int op;
  int x, y, w, h;
  int to_y;
  unsigned code;
  uint32_t fg, bg;
  uint32_t flags;
  /* IMAGE op: RGBA pixels owned by the queue while it is being drained.  */
  const void *ptr;
  unsigned ptr_len;
};

struct ohos_host_api
{
  uint32_t abi_version;

  struct
  {
    void (*metrics) (int width, int height, int cell_width, int cell_height,
		     int ascent, int descent);
    void (*title) (const char *title);
    /* Base64 payload produced by Emacs for the system pasteboard.  */
    void (*clipboard) (const char *base64);
    void (*log) (const char *line);
    /* Wake the ArkTS thread because a host command, title or clipboard
       update is pending.  Called from the Emacs thread.  */
    void (*host_wakeup) (void);
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
  void (*input_menubar) (int index);
  void (*input_raw) (const char *data, size_t len);
  void (*input_mouse) (int button, int x, int y,
		       unsigned modifiers, int press);
  void (*input_wheel) (int delta, int col, int row, unsigned modifiers);
  void (*input_motion) (int x, int y, unsigned modifiers);
  void (*input_focus) (int focused);
  void (*input_resize) (int width, int height);
  void (*input_expose) (int x, int y, int width, int height);
  /* Surface lifecycle, called on the ArkUI thread.  These only enqueue
     immutable events; the renderer is touched exclusively by Emacs.  */
  void (*surface_created) (void *window, int width, int height,
			   uint64_t generation);
  void (*surface_resized) (int width, int height, uint64_t generation);
  void (*surface_destroyed) (uint64_t generation);
  void (*font_metrics) (int ascent, int descent, int height,
			int space_width, int underline_position,
			int underline_thickness);
  /* Display metrics from ArkUI: physical DPI, logical density, font
     scale, and exact x/y DPI.  May be called before `run'.  */
  void (*set_display_metrics) (double density_dpi, double density_pixels,
			       double scaled_density, double xdpi,
			       double ydpi);

  int (*take_command) (char *buf, int cap);
  void (*resolve_command) (int id, int index, const char *text);

};

/* Returns NULL when HOST_ABI_VERSION does not match this build.  */
const struct ohos_emacs_api *ohos_emacs_api (uint32_t host_abi_version);

#ifdef __cplusplus
}
#endif

#endif /* OHOS_HOST_ABI_H */
