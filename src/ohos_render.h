/* Per-frame OHOS renderer C ABI.

   One `struct ohos_renderer' belongs to exactly one Emacs frame and owns
   that frame's surface binding, backing bitmap and frame transaction.
   Every entry point must be called from the Emacs thread; the host must
   not include this header or link the renderer archive.  */

#ifndef OHOS_RENDER_H
#define OHOS_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct ohos_renderer;

/* Immutable surface lifecycle event delivered by the UI thread.  The
   Emacs thread routes it to the context referenced by FRAME_ID.  */
enum OhosSurfaceEventKind
{
  OHOS_SURFACE_CREATED = 0,
  OHOS_SURFACE_RESIZED = 1,
  OHOS_SURFACE_DESTROYED = 2
};

struct OhosSurfaceEvent
{
  enum OhosSurfaceEventKind kind;
  uint32_t frame_id;
  void *window;
  int surface_w;
  int surface_h;
  uint64_t generation;
};

/* Bit flags returned by ohos_renderer_present.  A caller that sees
   OHOS_RENDER_FULL_REPAINT must mark the frame garbaged and redisplay it
   once: the backing bitmap was rebuilt from scratch.  */
#define OHOS_RENDER_PRESENTED 1
#define OHOS_RENDER_FULL_REPAINT 2

struct ohos_renderer *ohos_renderer_create (void);
void ohos_renderer_destroy (struct ohos_renderer *r);

/* Surface events, applied only on the Emacs thread.  CREATED binds the
   window, RESIZED only updates surface geometry, DESTROYED invalidates
   the surface.  Stale generations are discarded.  */
void ohos_renderer_apply_surface_event (struct ohos_renderer *r,
					const struct OhosSurfaceEvent *event);

/* Frame content rect and cell metrics, from the Emacs frame.  */
void ohos_renderer_set_frame_size (struct ohos_renderer *r, int frame_w,
				   int frame_h, int cell_w, int cell_h);

/* Frame transaction.  begin_frame is the only entry point allowed to
   rebuild the backing store; end_frame marks the content complete;
   present commits one complete frame.  */
int ohos_renderer_begin_frame (struct ohos_renderer *r);
void ohos_renderer_clear (struct ohos_renderer *r, uint32_t bg);
void ohos_renderer_fill (struct ohos_renderer *r, int x, int y, int w,
			 int h, uint32_t color);
void ohos_renderer_text (struct ohos_renderer *r, int x, int y, int w,
			 int h, unsigned code, uint32_t fg, uint32_t bg,
			 uint32_t flags);
void ohos_renderer_image (struct ohos_renderer *r, int x, int y, int w,
			  int h, const void *rgba, unsigned len);
void ohos_renderer_cursor (struct ohos_renderer *r, int x, int y, int w,
			   int h, int bar, uint32_t fg, uint32_t bg,
			   uint32_t flags);
void ohos_renderer_scroll (struct ohos_renderer *r, int x, int y, int w,
			   int h, int dy);
void ohos_renderer_end_frame (struct ohos_renderer *r);
int ohos_renderer_present (struct ohos_renderer *r);

/* Diagnostics.  `ready' is true when the context can accept drawing.  */
int ohos_renderer_ready (const struct ohos_renderer *r);

#ifdef __cplusplus
}
#endif

#endif /* OHOS_RENDER_H */
