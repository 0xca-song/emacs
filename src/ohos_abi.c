/* Versioned entry point exported by libemacs_gui.so.

   The host resolves exactly one symbol, `ohos_emacs_api', checks the ABI
   version, then calls `run' with its own host API.  Everything else in
   this library stays hidden at link time.  */

#include <config.h>

#include <stddef.h>

#include "lisp.h"
#include "ohosgui.h"

extern int main (int, char **);

static int
ohos_abi_run (int argc, char **argv, const struct ohos_host_api *host)
{
  if (!host || host->abi_version != OHOS_GUI_ABI_VERSION)
    return -1;
  ohos_set_host_api (host);
  return main (argc, argv);
}

static const struct ohos_emacs_api emacs_api =
{
  .abi_version = OHOS_GUI_ABI_VERSION,
  .run = ohos_abi_run,
  .input_key = ohos_input_key,
  .input_text = ohos_input_text,
  .input_menubar = ohos_input_menubar,
  .input_raw = ohos_input_raw,
  .input_mouse = ohos_input_mouse,
  .input_wheel = ohos_input_wheel,
  .input_motion = ohos_input_motion,
  .input_focus = ohos_input_focus,
  .input_resize = ohos_input_resize,
  .input_expose = ohos_input_expose,
  .surface_created = ohos_surface_created,
  .surface_resized = ohos_surface_resized,
  .surface_destroyed = ohos_surface_destroyed,
  .close_request = ohos_input_close_request,
  .window_create_failed = ohos_input_window_create_failed,
  .font_metrics = ohos_font_metrics,
  .set_display_metrics = ohos_set_display_metrics,
  .take_command = ohos_host_take_command,
  .resolve_command = ohos_host_result,
};

const struct ohos_emacs_api *
ohos_emacs_api (uint32_t host_abi_version)
{
  if (host_abi_version != OHOS_GUI_ABI_VERSION)
    return NULL;
  return &emacs_api;
}
