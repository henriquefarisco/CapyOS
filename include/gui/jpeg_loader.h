#ifndef GUI_JPEG_LOADER_H
#define GUI_JPEG_LOADER_H

#include <stddef.h>
#include <stdint.h>

/* Decoded JPEG image in ARGB32 (0xAARRGGBB) format, allocated via kalloc. */
struct jpeg_image {
    uint32_t  width;
    uint32_t  height;
    uint32_t *pixels;   /* width * height pixels, row-major */
};

/* Decode a JPEG file from memory (baseline DCT / SOF0 only).
 * Returns 0 on success; out->pixels is kalloc'd and must be freed with kfree.
 * Returns non-zero on error (out is zeroed). */
int jpeg_decode(const uint8_t *data, size_t len, struct jpeg_image *out);

/* Free pixel buffer allocated by jpeg_decode. */
void jpeg_free(struct jpeg_image *img);

#endif /* GUI_JPEG_LOADER_H */
