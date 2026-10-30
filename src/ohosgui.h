#ifndef OHOS_GUI_H
#define OHOS_GUI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called by the font/redisplay backend.  Implemented by the native OHOS
   frame sink; the first version uses a cell command sink.  */
void ohos_frame_text (int x, int y, int width, unsigned code,
                      uint32_t fg, uint32_t bg, int with_background);
#ifdef __cplusplus
}
#endif
#endif
