/* Versioned ABI between GNU Emacs' OHOS backend (libemacs_gui.so) and the
   HarmonyOS host (libentry.so).

   There are exactly two directions:

     struct ohos_emacs_api  host -> Emacs: lifecycle, input, surface
                            events and command channel.
     struct ohos_host_api   Emacs -> host: per-frame properties, logging,
                            clipboard and wakeups.  Drawing does not cross
                            this ABI; it goes to the statically linked
                            renderer on the Emacs thread.

   The host resolves the single exported symbol `ohos_emacs_api' and calls
   its `run' entry with the host API.  No other C symbol is part of the
   contract; both libraries hide everything else at link time.

   Every surface or input callback carries the `frame_id' assigned by the
   Emacs side, so the backend can route it to one frame context without
   consulting the global selected frame.  The one XComponent host uses
   frame_id 1 for its boot window.

   All Emacs -> host UI operations are typed.  Variable-length payloads
   (menu JSON, notification text) are passed as pointer + length and
   copied by the host before the call returns.  Interactive requests
   answer through menu_result or request_result, keyed by request id.  */

#ifndef OHOS_HOST_ABI_H
#define OHOS_HOST_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 12: per-frame window lifecycle and close/create notifications.  */
#define OHOS_GUI_ABI_VERSION 13

enum ohos_cursor_kind
{
  OHOS_HOST_CURSOR_DEFAULT = 0,
  OHOS_HOST_CURSOR_TEXT,
  OHOS_HOST_CURSOR_HAND,
  OHOS_HOST_CURSOR_HOURGLASS,
  OHOS_HOST_CURSOR_VDRAG,
  OHOS_HOST_CURSOR_HDRAG,
  OHOS_HOST_CURSOR_NONE
};

enum ohos_host_request_kind
{
  OHOS_REQUEST_CLIPBOARD_GET = 0,
  OHOS_REQUEST_BATTERY,
  OHOS_REQUEST_PICK_USER_DIR,
  OHOS_REQUEST_NOTIFY
};

struct ohos_window_params
{
  int width;
  int height;
  int x;
  int y;
  uint32_t parent_frame_id;
  int undecorated;
  int dialog;
  const char *title;
};

struct ohos_host_api
{
  uint32_t abi_version;

  struct
  {
    /* Exact content geometry of FRAME_ID.  The host may size its window
       from this, but never scales or shifts the rendered content.  */
    void (*metrics) (uint32_t frame_id, int width, int height, int cell_width,
		     int cell_height, int ascent, int descent);
    void (*title) (uint32_t frame_id, const char *title);
    /* Per-frame native window lifecycle.  create_window is asynchronous:
       the host queues it, returns 0 when accepted, and later reports
       surface_created or window_create_failed.  */
    int (*create_window) (uint32_t frame_id,
			  const struct ohos_window_params *params);
    void (*destroy_window) (uint32_t frame_id);
    void (*raise_window) (uint32_t frame_id);
    void (*set_window_title) (uint32_t frame_id, const char *title);
    void (*set_window_size) (uint32_t frame_id, int width, int height);
    void (*set_window_position) (uint32_t frame_id, int x, int y);
    void (*lower_window) (uint32_t frame_id);
    void (*set_visible) (uint32_t frame_id, int visible);
    void (*set_fullscreen) (uint32_t frame_id, int fullscreen);
    void (*minimize_window) (uint32_t frame_id);
    void (*set_alpha) (uint32_t frame_id, int percent);
    void (*set_cursor) (uint32_t frame_id, int cursor);
    void (*set_hourglass) (uint32_t frame_id, int on);
    void (*bell) (uint32_t frame_id);
    void (*pointer_warp) (uint32_t frame_id, int x, int y);
    void (*set_no_focus_on_map) (uint32_t frame_id, int no_focus);
    void (*set_no_accept_focus) (uint32_t frame_id, int no_accept);
    /* Publish/show/close a menu.  TEXT is JSON; the host copies it.  */
    int (*menu_bar) (uint32_t frame_id, const char *text, size_t len);
    int (*menu_show) (uint32_t frame_id, int menu_id, int flags, int dialog,
		      const char *text, size_t len);
    void (*menu_close) (uint32_t frame_id, int menu_id);
    /* ID-based system request.  Payload is request-specific text.  */
    int (*request) (uint32_t frame_id, int kind, int request_id,
		    const char *text, size_t len);
    /* Base64 payload produced by Emacs for the system pasteboard.  */
    void (*clipboard) (const char *base64);
    void (*log) (const char *line);
    /* Wake the ArkTS thread because a typed host event, title or clipboard
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

  int (*input_key) (uint32_t frame_id, unsigned key, unsigned modifiers,
		    int pressed);
  void (*input_text) (uint32_t frame_id, const char *text);
  void (*input_menubar) (uint32_t frame_id, int index);
  void (*input_raw) (uint32_t frame_id, const char *data, size_t len);
  void (*input_mouse) (uint32_t frame_id, int button, int x, int y,
		       unsigned modifiers, int press);
  void (*input_wheel) (uint32_t frame_id, int delta, int col, int row,
		       unsigned modifiers);
  void (*input_motion) (uint32_t frame_id, int x, int y,
			unsigned modifiers);
  void (*input_focus) (uint32_t frame_id, int focused);
  /* Legacy explicit resize request.  New hosts should only report surface
     events; input_resize remains for compatibility and routes to the
     same frame resize path.  */
  void (*input_resize) (uint32_t frame_id, int width, int height);
  void (*input_expose) (uint32_t frame_id, int x, int y, int width,
			int height);
  /* Surface lifecycle, called on the ArkUI thread.  These only enqueue
     immutable events; the renderer is touched exclusively by Emacs.  */
  void (*surface_created) (uint32_t frame_id, void *window, int width,
			   int height, uint64_t generation);
  void (*surface_resized) (uint32_t frame_id, int width, int height,
			   uint64_t generation);
  void (*surface_destroyed) (uint32_t frame_id, uint64_t generation);
  /* The user clicked the window close button.  Emacs must decide whether
     to delete the frame; the host does not destroy the window directly.  */
  void (*close_request) (uint32_t frame_id);
  /* The host could not create the native window for FRAME_ID.  */
  void (*window_create_failed) (uint32_t frame_id, const char *reason);
  void (*font_metrics) (int ascent, int descent, int height,
			int space_width, int underline_position,
			int underline_thickness);
  /* Display metrics from ArkUI: physical DPI, logical density, font
     scale, and exact x/y DPI.  May be called before `run'.  */
  void (*set_display_metrics) (double density_dpi, double density_pixels,
			       double scaled_density, double xdpi,
			       double ydpi);

  void (*menu_result) (int menu_id, int index);
  void (*request_result) (int request_id, int status, const char *text,
			  size_t len);

};

/* Returns NULL when HOST_ABI_VERSION does not match this build.  */
const struct ohos_emacs_api *ohos_emacs_api (uint32_t host_abi_version);

#ifdef __cplusplus
}
#endif

#endif /* OHOS_HOST_ABI_H */
