/* libcapyhtml/font.h -- F3.3c slice 4-final (branch feature/m5-w4).
 *
 * Embedded 8x8 ASCII bitmap font used by the userland rasterizer.
 * Mirrors `font8x8_basic` from the kernel (`src/gui/core/font8x8_data.c`)
 * to keep the visual appearance consistent with the kernel terminal,
 * but lives independently in ring 3 so that the engine never touches
 * kernel memory.
 *
 * The table covers codepoints 0..127. Anything outside that range
 * resolves to the '?' glyph. The library is purely consumer-side:
 * pixel rasterization lives in `capyhtml/raster.h`.
 */
#ifndef CAPYHTML_FONT_H
#define CAPYHTML_FONT_H

#include "capyhtml/render.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAPYHTML_FONT_GLYPH_W 8
#define CAPYHTML_FONT_GLYPH_H 8

/* Returns 8 bytes of bitmap (row 0 = top, MSB = leftmost pixel) for
 * the given byte. Out-of-range bytes (>= 128) return the '?' glyph
 * so the rasterizer never has to range-check. */
const uint8_t *capyhtml_font_glyph_row(uint8_t ch);

/* Fills a `capyhtml_font_ops` bound to the embedded 8x8 font. The
 * resulting struct uses no allocator and references only static
 * tables; safe to pass to `capyhtml_layout` from any thread. */
void capyhtml_font_ops_default(struct capyhtml_font_ops *out);

#ifdef __cplusplus
}
#endif

#endif /* CAPYHTML_FONT_H */
