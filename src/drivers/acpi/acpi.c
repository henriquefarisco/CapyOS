/**
 * ACPI Driver Implementation for NoirOS
 * Parses ACPI tables and provides power management functions.
 */
#include "drivers/acpi/acpi.h"
#include "arch/x86/hw/io.h"

#include <stddef.h>

/* ACPI PM1 Control Register bits */
#define ACPI_SLP_EN (1 << 13)
#define ACPI_SLP_TYP_S5                                                        \
  (5 << 10) /* Common S5 value, will be overridden if found */

/* Global ACPI state */
static int g_acpi_initialized = 0;
static uint16_t g_pm1a_cnt = 0;
static uint16_t g_pm1b_cnt = 0;
static uint16_t g_slp_typa = ACPI_SLP_TYP_S5;
static uint16_t g_slp_typb = ACPI_SLP_TYP_S5;
static struct acpi_gas g_reset_reg = {0};
static uint8_t g_reset_value = 0;

/* String comparison helper */
static int mem_compare(const void *a, const void *b, size_t len) {
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  for (size_t i = 0; i < len; ++i) {
    if (pa[i] != pb[i])
      return pa[i] - pb[i];
  }
  return 0;
}

/* Validate ACPI table checksum */
static int acpi_checksum_valid(const void *table, size_t len) {
  const uint8_t *bytes = (const uint8_t *)table;
  uint8_t sum = 0;
  for (size_t i = 0; i < len; ++i) {
    sum += bytes[i];
  }
  return sum == 0;
}

/* Search for RSDP in a memory range */
static struct acpi_rsdp *find_rsdp_in_range(uint32_t start, uint32_t end) {
  /* RSDP is 16-byte aligned */
  for (uint32_t addr = start; addr < end; addr += 16) {
    struct acpi_rsdp *rsdp = (struct acpi_rsdp *)(uintptr_t)addr;
    if (mem_compare(rsdp->signature, "RSD PTR ", 8) == 0) {
      /* Validate checksum (first 20 bytes for ACPI 1.0) */
      if (acpi_checksum_valid(rsdp, 20)) {
        return rsdp;
      }
    }
  }
  return NULL;
}

/* Find RSDP */
static struct acpi_rsdp *find_rsdp(void) {
  struct acpi_rsdp *rsdp;

  /* Search EBDA (Extended BIOS Data Area) - first 1KB */
  uint16_t ebda_seg = *(uint16_t *)(uintptr_t)0x40E;
  uint32_t ebda_addr = (uint32_t)ebda_seg << 4;
  if (ebda_addr) {
    rsdp = find_rsdp_in_range(ebda_addr, ebda_addr + 1024);
    if (rsdp)
      return rsdp;
  }

  /* Search BIOS ROM area: 0xE0000 - 0xFFFFF */
  rsdp = find_rsdp_in_range(0xE0000, 0x100000);
  return rsdp;
}

/* Find a table in RSDT by signature */
static struct acpi_header *find_table(struct acpi_rsdp *rsdp, const char *sig) {
  if (!rsdp)
    return NULL;

  struct acpi_header *rsdt =
      (struct acpi_header *)(uintptr_t)rsdp->rsdt_address;
  if (!rsdt)
    return NULL;

  /* Validate RSDT signature */
  if (mem_compare(rsdt->signature, "RSDT", 4) != 0) {
    return NULL;
  }

  /* Number of entries in RSDT */
  size_t entries =
      (rsdt->length - sizeof(struct acpi_header)) / sizeof(uint32_t);
  uint32_t *table_ptrs =
      (uint32_t *)((uint8_t *)rsdt + sizeof(struct acpi_header));

  for (size_t i = 0; i < entries; ++i) {
    struct acpi_header *table = (struct acpi_header *)(uintptr_t)table_ptrs[i];
    if (table && mem_compare(table->signature, sig, 4) == 0) {
      return table;
    }
  }
  return NULL;
}

/* Parse _S5 object from DSDT to find SLP_TYPa value */
static int parse_s5_from_dsdt(const uint8_t *dsdt, uint32_t length,
                              uint16_t *slp_typa) {
  /* Search for "_S5_" in DSDT (AML encoded) */
  for (uint32_t i = 0; i + 4 < length; ++i) {
    if (dsdt[i] == '_' && dsdt[i + 1] == 'S' && dsdt[i + 2] == '5' &&
        dsdt[i + 3] == '_') {
      /* Found _S5_ object, parse package */
      /* Skip to package contents - this is simplified AML parsing */
      /* Format varies but typically: _S5_ PackageOp PkgLength NumElements
       * BytePrefix Value */
      uint32_t j = i + 4;

      /* Skip any name path or method wrapper */
      while (j < length && j < i + 30) {
        if (dsdt[j] == 0x12) { /* PackageOp */
          j++;                 /* Skip PackageOp */
          /* Skip PkgLength (1-4 bytes) */
          uint8_t lead = dsdt[j];
          if ((lead & 0xC0) == 0) {
            j += 1; /* 1-byte length */
          } else {
            j += (lead >> 6) + 1; /* Multi-byte length */
          }
          j++; /* Skip NumElements */

          /* Now read the first element (SLP_TYPa) */
          if (j < length) {
            if (dsdt[j] == 0x0A) { /* BytePrefix */
              *slp_typa = ((uint16_t)dsdt[j + 1]) << 10;
              return 0;
            } else if (dsdt[j] <= 0x0F) { /* Small integer 0-15 */
              *slp_typa = ((uint16_t)dsdt[j]) << 10;
              return 0;
            }
          }
          break;
        }
        j++;
      }
    }
  }
  return -1; /* Not found, use default */
}

int acpi_init(void) {
  if (g_acpi_initialized)
    return 0;

  /* Find RSDP */
  struct acpi_rsdp *rsdp = find_rsdp();
  if (!rsdp) {
    return -1;
  }

  /* Find FADT */
  struct acpi_fadt *fadt = (struct acpi_fadt *)find_table(rsdp, "FACP");
  if (!fadt) {
    return -1;
  }

  /* Get PM1a_CNT and PM1b_CNT addresses */
  g_pm1a_cnt = (uint16_t)fadt->pm1a_control_block;
  g_pm1b_cnt = (uint16_t)fadt->pm1b_control_block;

  if (g_pm1a_cnt == 0) {
    return -1;
  }

  /* Try to find _S5 SLP_TYPa from DSDT */
  if (fadt->dsdt) {
    struct acpi_header *dsdt = (struct acpi_header *)(uintptr_t)fadt->dsdt;
    if (dsdt && mem_compare(dsdt->signature, "DSDT", 4) == 0) {
      uint16_t slp_typ;
      if (parse_s5_from_dsdt((const uint8_t *)dsdt, dsdt->length, &slp_typ) ==
          0) {
        g_slp_typa = slp_typ;
        g_slp_typb = slp_typ;
      }
    }
  }

  /* Check for ACPI 2.0+ Reset Register */
  if (fadt->header.length >= 129) {
    g_reset_reg = fadt->reset_reg;
    g_reset_value = fadt->reset_value;
  }

  g_acpi_initialized = 1;
  return 0;
}

void acpi_shutdown(void) {
  if (!g_acpi_initialized) {
    acpi_init();
  }

  if (g_pm1a_cnt) {
    /* Write S5 sleep command to PM1a_CNT */
    outw(g_pm1a_cnt, g_slp_typa | ACPI_SLP_EN);

    /* If PM1b_CNT exists, write to it too */
    if (g_pm1b_cnt) {
      outw(g_pm1b_cnt, g_slp_typb | ACPI_SLP_EN);
    }
  }

  /* Fallback: QEMU/Bochs exit ports */
  outw(0x604, 0x2000);  /* QEMU isa-debug-exit */
  outw(0xB004, 0x2000); /* Bochs/older QEMU */

  /* Last resort: halt */
  while (1) {
    __asm__ volatile("hlt");
  }
}

void acpi_reboot(void) {
  __asm__ volatile("cli");

  /* Method 0: ACPI FADT Reset (Hyper-V preferred) */
  if (g_acpi_initialized &&
      g_reset_reg.address_space_id == 1) { /* 1 = I/O Port */
    outb((uint16_t)g_reset_reg.address, g_reset_value);
    /* Wait a bit */
    for (volatile int i = 0; i < 10000; i++)
      ;
  }

  /* Method 1: 8042 keyboard controller reset */
  uint8_t good = 0x02;
  while (good & 0x02) {
    good = inb(0x64);
  }
  outb(0x64, 0xFE);

  /* Small delay */
  for (volatile int i = 0; i < 10000; i++)
    ;

  /* Method 2: Triple fault */
  struct {
    uint16_t limit;
    uint32_t base;
  } __attribute__((packed)) null_idt = {0, 0};
  __asm__ volatile("lidt %0" : : "m"(null_idt));
  __asm__ volatile("int $0x03");

  while (1) {
    __asm__ volatile("hlt");
  }
}

int acpi_is_available(void) { return g_acpi_initialized; }
