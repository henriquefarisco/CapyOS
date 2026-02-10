/* PCIe configuration space access for x86_64 via ECAM (Memory-mapped) and legacy I/O.
 * ECAM base address is typically obtained from ACPI MCFG table.
 */
#ifndef PCIE_H
#define PCIE_H

#include <stdint.h>

/* PCI configuration space offsets */
#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_CLASS_REVISION  0x08
#define PCI_HEADER_TYPE     0x0E
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_BAR2            0x18
#define PCI_BAR3            0x1C
#define PCI_BAR4            0x20
#define PCI_BAR5            0x24
#define PCI_INTERRUPT_LINE  0x3C

/* PCI command register bits */
#define PCI_CMD_IO_SPACE     0x0001
#define PCI_CMD_MEMORY_SPACE 0x0002
#define PCI_CMD_BUS_MASTER   0x0004
#define PCI_CMD_INT_DISABLE  0x0400

/* Class codes */
#define PCI_CLASS_STORAGE    0x01
#define PCI_SUBCLASS_NVME    0x08  /* NVMe controller */

/* Maximum scan limits */
#define PCI_MAX_BUS  256
#define PCI_MAX_DEV  32
#define PCI_MAX_FUNC 8

struct pci_device {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint64_t bar[6];
};

/* Legacy I/O port access (works without MCFG) */
#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static inline uint32_t pci_io_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    return (uint32_t)(0x80000000 |
                      ((uint32_t)bus << 16) |
                      ((uint32_t)(dev & 0x1F) << 11) |
                      ((uint32_t)(func & 0x07) << 8) |
                      (offset & 0xFC));
}

/* Read 32 bits from PCI config space via I/O ports */
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t  pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value);

/* Initialize PCI subsystem */
void pci_init(void);

/* Find a device by class/subclass. Returns 0 on success, -1 if not found. */
int pci_find_device(uint8_t class_code, uint8_t subclass, struct pci_device *out);

/* Find NVMe controller specifically */
int pci_find_nvme(struct pci_device *out);

/* Get 64-bit BAR address (for 64-bit BARs) */
uint64_t pci_read_bar64(uint8_t bus, uint8_t dev, uint8_t func, int bar_index);

#endif /* PCIE_H */
