/* PCIe configuration space access implementation for x86_64.
 * Uses legacy I/O port method (0xCF8/0xCFC) which works universally.
 */
#include "drivers/pcie.h"
#include <stdint.h>

/* I/O port access - these work on both x86 and x86_64 */
static inline void outl(uint16_t port, uint32_t val) {
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
  uint32_t ret;
  __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func,
                           uint8_t offset) {
  uint32_t addr = pci_io_addr(bus, dev, func, offset);
  outl(PCI_CONFIG_ADDR, addr);
  return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func,
                           uint8_t offset) {
  uint32_t val = pci_config_read32(bus, dev, func, offset & 0xFC);
  return (uint16_t)(val >> ((offset & 2) * 8));
}

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func,
                         uint8_t offset) {
  uint32_t val = pci_config_read32(bus, dev, func, offset & 0xFC);
  return (uint8_t)(val >> ((offset & 3) * 8));
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset,
                        uint32_t value) {
  uint32_t addr = pci_io_addr(bus, dev, func, offset);
  outl(PCI_CONFIG_ADDR, addr);
  outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset,
                        uint16_t value) {
  uint32_t addr = pci_io_addr(bus, dev, func, offset & 0xFC);
  outl(PCI_CONFIG_ADDR, addr);
  uint32_t existing = inl(PCI_CONFIG_DATA);
  int shift = (offset & 2) * 8;
  existing &= ~(0xFFFFu << shift);
  existing |= ((uint32_t)value << shift);
  outl(PCI_CONFIG_DATA, existing);
}

uint64_t pci_read_bar64(uint8_t bus, uint8_t dev, uint8_t func, int bar_index) {
  uint8_t offset = (uint8_t)(PCI_BAR0 + bar_index * 4);
  uint32_t bar_lo = pci_config_read32(bus, dev, func, offset);

  /* Check if 64-bit BAR (bits 2:1 = 10b) */
  if ((bar_lo & 0x06) == 0x04) {
    uint32_t bar_hi = pci_config_read32(bus, dev, func, (uint8_t)(offset + 4));
    /* Memory BAR: mask off type bits */
    uint64_t addr = ((uint64_t)bar_hi << 32) | (bar_lo & ~0xFull);
    return addr;
  }

  /* 32-bit BAR */
  if (bar_lo & 0x01) {
    /* I/O BAR */
    return bar_lo & ~0x3ull;
  }
  /* Memory BAR */
  return bar_lo & ~0xFull;
}

static int g_pci_initialized = 0;

void pci_init(void) {
  g_pci_initialized = 1;
  /* PCI doesn't require explicit initialization with legacy I/O */
}

int pci_find_device(uint8_t class_code, uint8_t subclass,
                    struct pci_device *out) {
  for (uint16_t bus = 0; bus < PCI_MAX_BUS; bus++) {
    for (uint8_t dev = 0; dev < PCI_MAX_DEV; dev++) {
      for (uint8_t func = 0; func < PCI_MAX_FUNC; func++) {
        uint16_t vendor =
            pci_config_read16((uint8_t)bus, dev, func, PCI_VENDOR_ID);
        if (vendor == 0xFFFF || vendor == 0x0000) {
          if (func == 0)
            break; /* No device, skip other functions */
          continue;
        }

        uint32_t class_rev =
            pci_config_read32((uint8_t)bus, dev, func, PCI_CLASS_REVISION);
        uint8_t dev_class = (uint8_t)(class_rev >> 24);
        uint8_t dev_subclass = (uint8_t)(class_rev >> 16);
        uint8_t prog_if = (uint8_t)(class_rev >> 8);

        if (dev_class == class_code && dev_subclass == subclass) {
          if (out) {
            out->bus = (uint8_t)bus;
            out->device = dev;
            out->function = func;
            out->vendor_id = vendor;
            out->device_id =
                pci_config_read16((uint8_t)bus, dev, func, PCI_DEVICE_ID);
            out->class_code = dev_class;
            out->subclass = dev_subclass;
            out->prog_if = prog_if;
            /* Read BARs */
            for (int i = 0; i < 6; i++) {
              out->bar[i] = pci_read_bar64((uint8_t)bus, dev, func, i);
            }
          }
          return 0;
        }

        /* Check if multi-function device */
        if (func == 0) {
          uint8_t header =
              pci_config_read8((uint8_t)bus, dev, func, PCI_HEADER_TYPE);
          if ((header & 0x80) == 0)
            break; /* Single function */
        }
      }
    }
  }
  return -1;
}

int pci_find_nvme(struct pci_device *out) {
  /* NVMe: Class 01h (Storage), Subclass 08h (NVM), Prog IF 02h (NVM Express) */
  return pci_find_device(PCI_CLASS_STORAGE, PCI_SUBCLASS_NVME, out);
}
