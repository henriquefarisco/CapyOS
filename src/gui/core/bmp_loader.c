#include "gui/bmp_loader.h"
#include "fs/vfs.h"
#include "memory/kmem.h"
#include <stddef.h>

struct bmp_file_header {
  uint16_t type;
  uint32_t size;
  uint16_t reserved1;
  uint16_t reserved2;
  uint32_t offset;
} __attribute__((packed));

struct bmp_info_header {
  uint32_t size;
  int32_t width;
  int32_t height;
  uint16_t planes;
  uint16_t bpp;
  uint32_t compression;
  uint32_t image_size;
  int32_t x_ppm;
  int32_t y_ppm;
  uint32_t colors_used;
  uint32_t colors_important;
} __attribute__((packed));

int bmp_load(const uint8_t *data, size_t size, struct bmp_image *out) {
  if (!data || !out || size < 54) return -1;
  const struct bmp_file_header *fh = (const struct bmp_file_header *)data;
  if (fh->type != 0x4D42) return -1;
  const struct bmp_info_header *ih = (const struct bmp_info_header *)(data + 14);
  if (ih->bpp != 24 && ih->bpp != 32) return -1;
  if (ih->compression != 0) return -1;
  int32_t w = ih->width;
  int32_t h = ih->height;
  int bottom_up = (h > 0) ? 1 : 0;
  if (h < 0) h = -h;
  if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;
  out->width = (uint32_t)w;
  out->height = (uint32_t)h;
  out->pixels = (uint32_t *)kmalloc((uint32_t)w * (uint32_t)h * 4);
  if (!out->pixels) return -1;
  uint32_t bpp = ih->bpp;
  uint32_t row_size = ((bpp * (uint32_t)w + 31) / 32) * 4;
  uint32_t pixel_offset = fh->offset;
  for (int32_t y = 0; y < h; y++) {
    int32_t src_y = bottom_up ? (h - 1 - y) : y;
    const uint8_t *row = data + pixel_offset + (uint32_t)src_y * row_size;
    if ((size_t)((row - data) + row_size) > size) break;
    for (int32_t x = 0; x < w; x++) {
      uint32_t pixel;
      if (bpp == 32) {
        uint32_t off = (uint32_t)x * 4;
        pixel = ((uint32_t)row[off+2]<<16)|((uint32_t)row[off+1]<<8)|((uint32_t)row[off]);
      } else {
        uint32_t off = (uint32_t)x * 3;
        pixel = ((uint32_t)row[off+2]<<16)|((uint32_t)row[off+1]<<8)|((uint32_t)row[off]);
      }
      out->pixels[y * (uint32_t)w + x] = pixel;
    }
  }
  return 0;
}

int bmp_load_from_file(const char *path, struct bmp_image *out) {
  if (!path || !out) return -1;
  struct file *f = vfs_open(path, 0x1);
  if (!f) return -1;
  struct vfs_stat st;
  if (vfs_stat_path(path, &st) != 0) { vfs_close(f); return -1; }
  if (st.size < 54 || st.size > 16 * 1024 * 1024) { vfs_close(f); return -1; }
  uint8_t *buf = (uint8_t *)kmalloc(st.size);
  if (!buf) { vfs_close(f); return -1; }
  long rd = vfs_read(f, buf, st.size);
  vfs_close(f);
  if (rd < 54) { kfree(buf); return -1; }
  int r = bmp_load(buf, (size_t)rd, out);
  kfree(buf);
  return r;
}
