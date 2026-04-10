#ifndef DRIVERS_GPU_CORE_H
#define DRIVERS_GPU_CORE_H

#include <stdint.h>
#include <stddef.h>

#define GPU_MAX_MODES 8
#define GPU_VENDOR_NVIDIA 0x10DE
#define GPU_VENDOR_AMD    0x1002
#define GPU_VENDOR_INTEL  0x8086

enum gpu_driver_type {
  GPU_DRIVER_NONE = 0,
  GPU_DRIVER_UEFI_GOP,
  GPU_DRIVER_NOUVEAU,
  GPU_DRIVER_GENERIC_VBE
};

struct gpu_mode {
  uint32_t width;
  uint32_t height;
  uint32_t bpp;
  uint32_t pitch;
};

struct gpu_device {
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t bus, dev, func;
  uint64_t bar0;
  uint64_t bar0_size;
  uint64_t vram_base;
  uint64_t vram_size;
  enum gpu_driver_type driver;
  struct gpu_mode current_mode;
  uint32_t *framebuffer;
  int detected;
  int initialized;
};

struct gpu_info {
  struct gpu_device device;
  struct gpu_mode modes[GPU_MAX_MODES];
  uint32_t mode_count;
};

void gpu_init(void);
int gpu_detect(void);
int gpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
void gpu_get_info(struct gpu_info *out);
uint32_t *gpu_get_framebuffer(void);
int gpu_is_nvidia(void);
const char *gpu_driver_name(void);

#endif /* DRIVERS_GPU_CORE_H */
