/*
 * userland/bin/capybrowse/browser_image.c — CapyOS image decode adapter over
 * CapyCodecs `capy-codec-image` v2 (Etapa 7 / Slice 7.4). See browser_image.h.
 *
 * Injects a bounded bump allocator (static .bss arena, reset per decode) and a
 * tinf-backed zlib inflater (for PNG/ICO) into the decoupled CapyCodecs core.
 * Freestanding: host-testable and ring-3 linkable.
 */
#include "browser_image.h"

#include "capy_image.h" /* CapyCodecs: capy_image_decode_memory, ... */
#include "tinf.h"       /* in-tree inflater: tinf_zlib_uncompress, tinf_init */

/* Bounded arena: must hold the ARGB32 output (w*h*4) PLUS the decoder's
 * temporaries (e.g. PNG scanline reconstruction ~ w*h*4). 512 KiB + a 192x192
 * dimension cap keeps output+temp (~2*147 KiB) inside the arena with headroom;
 * a larger image fails CAPY_IMAGE_ERR_OUT_OF_MEMORY (fail-closed). */
#define CAPYOS_IMAGE_ARENA (512u * 1024u)
#define CAPYOS_IMAGE_MAX_DIM 192u

static uint8_t g_arena[CAPYOS_IMAGE_ARENA];
static size_t g_used;
static int g_tinf_ready;

static void *img_alloc(size_t size, void *user_data) {
  size_t aligned;
  void *p;
  (void)user_data;
  if (size == 0u) return 0;
  aligned = (size + 15u) & ~(size_t)15u; /* 16-byte align */
  if (aligned < size) return 0;          /* overflow */
  if (aligned > CAPYOS_IMAGE_ARENA - g_used) return 0; /* arena exhausted */
  p = g_arena + g_used;
  g_used += aligned;
  return p;
}

static void img_free(void *ptr, void *user_data) {
  (void)ptr;
  (void)user_data; /* bump arena: freed wholesale on the next decode's reset */
}

/* Wrap tinf (zlib) to the CapyCodecs inflater contract: 0 on success (with
 * *dest_len set to the inflated size), -1 on any failure. */
static int img_inflate(uint8_t *dest, size_t *dest_len, const uint8_t *source,
                       size_t source_len, void *user_data) {
  unsigned int dl;
  int rc;
  (void)user_data;
  if (!dest || !dest_len || !source) return -1;
  if (*dest_len > 0xFFFFFFFFu || source_len > 0xFFFFFFFFu) return -1;
  dl = (unsigned int)*dest_len;
  rc = tinf_zlib_uncompress(dest, &dl, source, (unsigned int)source_len);
  if (rc != TINF_OK) return -1;
  *dest_len = (size_t)dl;
  return 0;
}

int capyos_image_decode(const uint8_t *bytes, size_t len,
                        struct capyos_image *out) {
  struct capy_image_allocator allocator;
  struct capy_image_inflater inflater;
  struct capy_image_limits limits;
  struct capy_image_rgba32 rgba;

  if (out) {
    out->width = 0u;
    out->height = 0u;
    out->pixels = 0;
  }
  if (!bytes || len == 0u || !out) return -1;

  if (!g_tinf_ready) {
    tinf_init();
    g_tinf_ready = 1;
  }
  g_used = 0u; /* reset the bump arena: previous decode's pixels are now void */

  allocator.alloc = img_alloc;
  allocator.free = img_free;
  allocator.user_data = 0;
  inflater.inflate = img_inflate;
  inflater.user_data = 0;

  capy_image_default_limits(&limits);
  limits.max_width = CAPYOS_IMAGE_MAX_DIM;
  limits.max_height = CAPYOS_IMAGE_MAX_DIM;
  limits.max_output_bytes = CAPYOS_IMAGE_ARENA;
  limits.max_temporary_bytes = CAPYOS_IMAGE_ARENA;

  if (capy_image_decode_memory(bytes, len, &allocator, &inflater, &limits,
                               &rgba) != CAPY_IMAGE_OK)
    return -1;

  out->width = rgba.width;
  out->height = rgba.height;
  out->pixels = rgba.pixels; /* lives in g_arena; valid until the next decode */
  return 0;
}
