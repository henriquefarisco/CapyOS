#ifndef GUI_BMP_LOADER_H
#define GUI_BMP_LOADER_H

#include <stdint.h>
#include <stddef.h>

struct bmp_image {
  uint32_t *pixels;
  uint32_t width;
  uint32_t height;
};

int bmp_load(const uint8_t *data, size_t size, struct bmp_image *out);
int bmp_load_from_file(const char *path, struct bmp_image *out);

#endif
