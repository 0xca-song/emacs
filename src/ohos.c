/* OHOS native bridge: the NAPI-facing side of the backend.

   Mirrors the role of android.c: it is the only place that knows about
   the host callback ABI.  Emacs core code calls ohos_log/frame_text; the
   app installs callbacks with the setter functions.  */

#include <config.h>
#include "lisp.h"
#include "ohosgui.h"

static ohos_frame_sink_fn ohos_frame_sink;
static ohos_log_fn ohos_log_sink;

void
ohos_set_log_sink (ohos_log_fn fn)
{
  ohos_log_sink = fn;
}

void
ohos_log (const char *s)
{
  if (ohos_log_sink)
    ohos_log_sink (s);
}

void
ohos_set_frame_sink (ohos_frame_sink_fn fn)
{
  ohos_frame_sink = fn;
}

void
ohos_frame_text (int x, int y, int width, unsigned code, uint32_t fg,
                 uint32_t bg, int with_background)
{
  if (ohos_frame_sink)
    ohos_frame_sink (x, y, width, code, fg, bg, with_background);
}
