#include "drivers/gpu/gpu_core.h"
#include "drivers/pcie.h"
#include "core/klog.h"
#include <stddef.h>

static struct gpu_device g_gpu;
static int g_gpu_detected = 0;

static void gpu_memset(void *d, int v, size_t n) {
  uint8_t *p = (uint8_t *)d; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}

static inline uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
  uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                  ((uint32_t)func << 8) | (off & 0xFC);
  __asm__ volatile("outl %0, %1" : : "a"(addr), "Nd"((uint16_t)0xCF8));
  uint32_t val;
  __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"((uint16_t)0xCFC));
  return val;
}

void gpu_init(void) {
  gpu_memset(&g_gpu, 0, sizeof(g_gpu));
  g_gpu.driver = GPU_DRIVER_UEFI_GOP;
  g_gpu_detected = 0;
}

int gpu_detect(void) {
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint8_t dev = 0; dev < 32; dev++) {
      for (uint8_t func = 0; func < 8; func++) {
        uint32_t id = pci_read32((uint8_t)bus, dev, func, 0);
        if (id == 0xFFFFFFFF) continue;

        uint32_t class_reg = pci_read32((uint8_t)bus, dev, func, 0x08);
        uint8_t class_code = (uint8_t)(class_reg >> 24);
        uint8_t subclass = (uint8_t)(class_reg >> 16);

        if (class_code != 0x03) continue; /* Display controller */

        uint16_t vendor = (uint16_t)(id & 0xFFFF);
        uint16_t device = (uint16_t)(id >> 16);

        g_gpu.vendor_id = vendor;
        g_gpu.device_id = device;
        g_gpu.bus = (uint8_t)bus;
        g_gpu.dev = dev;
        g_gpu.func = func;
        g_gpu.detected = 1;

        uint32_t bar0 = pci_read32((uint8_t)bus, dev, func, 0x10);
        g_gpu.bar0 = (uint64_t)(bar0 & 0xFFFFFFF0);

        if (vendor == GPU_VENDOR_NVIDIA) {
          g_gpu.driver = GPU_DRIVER_NOUVEAU;
          klog(KLOG_INFO, "[gpu] NVIDIA GPU detected.");
          klog_hex(KLOG_INFO, "[gpu] Device ID: 0x", device);
        } else if (vendor == GPU_VENDOR_AMD) {
          klog(KLOG_INFO, "[gpu] AMD GPU detected.");
        } else if (vendor == GPU_VENDOR_INTEL) {
          klog(KLOG_INFO, "[gpu] Intel GPU detected.");
        } else {
          klog(KLOG_INFO, "[gpu] Unknown GPU vendor detected.");
        }

        klog_hex(KLOG_INFO, "[gpu] BAR0: 0x", g_gpu.bar0);
        g_gpu_detected = 1;
        return 1;
      }
    }
  }
  klog(KLOG_INFO, "[gpu] No discrete GPU found, using UEFI GOP framebuffer.");
  return 0;
}

int gpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
  /* For now, mode setting is only via UEFI GOP (already set by loader).
   * Nouveau mode setting would program CRTC/encoder/connector here. */
  g_gpu.current_mode.width = width;
  g_gpu.current_mode.height = height;
  g_gpu.current_mode.bpp = bpp;
  return 0;
}

void gpu_get_info(struct gpu_info *out) {
  if (!out) return;
  gpu_memset(out, 0, sizeof(*out));
  out->device = g_gpu;
  out->mode_count = 1;
  out->modes[0] = g_gpu.current_mode;
}

uint32_t *gpu_get_framebuffer(void) {
  return g_gpu.framebuffer;
}

int gpu_is_nvidia(void) {
  return (g_gpu_detected && g_gpu.vendor_id == GPU_VENDOR_NVIDIA) ? 1 : 0;
}

const char *gpu_driver_name(void) {
  switch (g_gpu.driver) {
    case GPU_DRIVER_NOUVEAU: return "nouveau";
    case GPU_DRIVER_UEFI_GOP: return "uefi-gop";
    case GPU_DRIVER_GENERIC_VBE: return "vbe";
    default: return "none";
  }
}
