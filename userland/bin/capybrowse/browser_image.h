/*
 * userland/bin/capybrowse/browser_image.h — CapyOS-side image decode adapter
 * over the decoupled CapyCodecs `capy-codec-image` v2 core (Etapa 7 / Slice 7.4).
 *
 * Turns encoded image bytes (BMP / PNG / JPEG / QOI / ICO, auto-detected) into
 * an ARGB32 pixel buffer, so the pixel rasterizer (browser_render_pixel) can
 * draw real images at a display-list IMAGE node instead of a placeholder. Per
 * the media-codec integration contract, CapyCodecs owns the portable decoders
 * (pure, allocation-free except via an injected allocator, with an injected
 * inflater for PNG zlib streams); CapyOS owns the allocator (a bounded bump
 * arena here) and the inflater (wrapping the in-tree tinf), and calls
 * `capy_image_decode_memory` directly — it does NOT go through CapyBrowser's
 * internal C3 host-adapter.
 *
 * Single-shot / NOT reentrant: each call resets the internal bump arena, so the
 * returned pixels are valid only until the next call. Pure (no I/O, no clock),
 * deterministic, fail-closed (bad args / unsupported / corrupt / oversized for
 * the arena -> -1). Host-testable and ring-3 linkable.
 */
#ifndef CAPYOS_BROWSER_IMAGE_H
#define CAPYOS_BROWSER_IMAGE_H

#include <stddef.h>
#include <stdint.h>

struct capyos_image {
  uint32_t width;
  uint32_t height;
  const uint32_t *pixels; /* ARGB32 (0xAARRGGBB), width*height, row-major;
                           * points into the adapter's arena (valid until the
                           * next capyos_image_decode call). */
};

/* Decode `bytes`[0..len) to ARGB32. Returns 0 on success (out filled), or -1
 * fail-closed on NULL/empty input, an unsupported/corrupt image, or one that
 * does not fit the bounded arena (CAPYOS_IMAGE_MAX_DIM / arena size). `out` is
 * always reset first. */
int capyos_image_decode(const uint8_t *bytes, size_t len,
                        struct capyos_image *out);

#endif /* CAPYOS_BROWSER_IMAGE_H */
