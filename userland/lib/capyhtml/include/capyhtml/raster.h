/* libcapyhtml/raster.h -- F3.3c slice 4-final.
 *
 * Pixel rasterizer that turns a `capyhtml_cmd[]` (produced by
 * `capyhtml_layout`) into BGRA8888 pixels in a caller-owned
 * framebuffer. The rasterizer is freestanding -- no malloc, no
 * libc, only stdint -- and walks the command list once, clipping
 * any draw that falls outside the target bounds.
 *
 * Design contracts:
 *
 *   - Target is BGRA8888 little-endian (matches the kernel
 *     framebuffer + capyos compositor + EVENT_FRAME wire format).
 *     The caller passes a `uint32_t argb` and the rasterizer
 *     writes it byte-by-byte as B,G,R,A which on x86 reproduces
 *     the same memory layout the compositor expects.
 *
 *   - The palette maps each `capyhtml_color_role` to an ARGB
 *     value plus a single background ARGB. The library never
 *     bakes in colors; themes (compositor `rosa`, `capyos`, etc.)
 *     pick the palette and the rasterizer applies it.
 *
 *   - Bold is implemented by a second blit shifted +1 px on x.
 *     Underline is a 1 px line at y + glyph_h*scale. Italic is
 *     out of scope for the MVP font.
 */
#ifndef CAPYHTML_RASTER_H
#define CAPYHTML_RASTER_H

#include "capyhtml/render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mutable framebuffer descriptor. `pixels` points at the top-left
 * of the area the rasterizer is allowed to write. `stride_b` is
 * the byte distance between two successive rows and must be
 * >= width_px * 4. */
struct capyhtml_raster_target {
    uint8_t *pixels;
    int32_t  width_px;
    int32_t  height_px;
    int32_t  stride_b;
};

/* Color resolution table. Index `[i]` matches `enum
 * capyhtml_color_role` value `i`. Five entries cover TEXT (0),
 * HEADING (1), LINK (2), MUTED (3), BULLET (4). */
struct capyhtml_palette {
    uint32_t color_argb[5];
    uint32_t background_argb;
};

/* Fill the entire target with `argb`. */
void capyhtml_raster_clear(const struct capyhtml_raster_target *target,
                           uint32_t argb);

/* Render one command. Bound-checked; commands fully outside the
 * target bounds are silently dropped. Unknown `kind` values are
 * ignored (forward-compat with future cmd kinds). */
void capyhtml_raster_draw(const struct capyhtml_raster_target *target,
                          const struct capyhtml_cmd          *cmd,
                          const struct capyhtml_palette      *palette);

/* One-shot: clears with `palette->background_argb` then walks
 * `cmds[0..count)` calling `capyhtml_raster_draw` on each. */
void capyhtml_raster_render(const struct capyhtml_raster_target *target,
                            const struct capyhtml_cmd           *cmds,
                            uint16_t                              cmd_count,
                            const struct capyhtml_palette       *palette);

#ifdef __cplusplus
}
#endif

#endif /* CAPYHTML_RASTER_H */
