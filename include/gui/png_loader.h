#ifndef GUI_PNG_LOADER_H
#define GUI_PNG_LOADER_H

#include <stddef.h>
#include <stdint.h>

/* Decoded PNG image in ARGB32 (0xAARRGGBB) format, allocated via kalloc. */
struct png_image {
    uint32_t  width;
    uint32_t  height;
    uint32_t *pixels;   /* width * height pixels, row-major */
};

/* Decode a PNG file from memory.
 * Returns 0 on success; out->pixels is kalloc'd and must be freed with kfree.
 * Returns non-zero on error (out is zeroed). */
int png_decode(const uint8_t *data, size_t len, struct png_image *out);

/* Free pixel buffer allocated by png_decode. */
void png_free(struct png_image *img);

#endif /* GUI_PNG_LOADER_H */
