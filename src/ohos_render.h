#ifndef OHOS_RENDER_H
#define OHOS_RENDER_H
#include "ohos_host_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
void ohos_render_notify_surface (void *window, int width, int height);
void ohos_render_notify_reset (void);
void ohos_render_set_content_size (int width, int height);
int ohos_render_begin_frame (void);
int ohos_render_submit (const struct ohos_draw_command *cmds, int n);
int ohos_render_flush (void);
int ohos_render_pending (void);
void ohos_render_set_expose_callback (void (*cb) (int, int, int, int));
#ifdef __cplusplus
}
#endif
#endif
