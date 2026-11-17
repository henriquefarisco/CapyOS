/**
 * ACPI Driver Implementation for CAPYOS
 * Parses ACPI tables and provides power management functions.
 */
#include "drivers/acpi/acpi.h"
#include "drivers/io.h"
#include "drivers/efi/efi_console.h"

#include <stddef.h>
#include "drivers/acpi/acpi_tables.h"
#include "boot/handoff.h"
#include "memory/vmm.h"

/* ACPI PM1 Control Register bits */
#define ACPI_SLP_EN (1 << 13)
#define ACPI_SLP_TYP_S5                                                        \
  (5 << 10) /* Common S5 value, will be overridden if found */

#define EFI_RESET_COLD_K 0u
#define EFI_RESET_SHUTDOWN_K 2u

typedef void(EFIAPI *efi_reset_system_fn_k)(uint32_t ResetType,
                                            EFI_STATUS_K ResetStatus,
                                            uint64_t DataSize,
                                            void *ResetData);

typedef struct {
  uint8_t Hdr[24];
  void *GetTime;
  void *SetTime;
  void *GetWakeupTime;
  void *SetWakeupTime;
  void *SetVirtualAddressMap;
  void *ConvertPointer;
  void *GetVariable;
  void *GetNextVariableName;
  void *SetVariable;
  void *GetNextHighMonotonicCount;
  efi_reset_system_fn_k ResetSystem;
} EFI_RUNTIME_SERVICES_K;

/* Global ACPI state */
int g_acpi_initialized = 0;
static uint16_t g_pm1a_cnt = 0;
static uint16_t g_pm1b_cnt = 0;
static uint16_t g_slp_typa = ACPI_SLP_TYP_S5;
static uint16_t g_slp_typb = ACPI_SLP_TYP_S5;
static struct acpi_gas g_reset_reg = {0};
static uint8_t g_reset_value = 0;
uint64_t g_rsdp_override = 0;
uint64_t g_efi_system_table = 0;

static const struct boot_handoff *g_acpi_handoff;
static struct acpi_power_info g_power;

/* Firmware tables are read only from reserved EFI RAM, never from arbitrary
 * physical addresses/MMIO. The active kernel tables must identity-map every
 * page of the validated span. The existing huge-page-aware mapping query
 * conservatively also requires writable leaves; no writes are performed.
 * Called on kernel-owned identity tables after vmm_init. */
static const uint8_t *map_firmware(void *unused, uint64_t addr, size_t length) {
  (void)unused;
  const struct boot_handoff *h = g_acpi_handoff;
  if (!h || !h->memmap || h->memmap_desc_size < 40 || !length ||
      addr < 4096 || addr >= (1ull << 47) || length > (1ull << 47) - addr ||
      h->memmap_entries > h->memmap_size / h->memmap_desc_size) return NULL;
  const uint8_t *map = (const uint8_t *)(uintptr_t)h->memmap;
  int found = 0;
  for (uint32_t i = 0; i < h->memmap_entries; ++i) {
    const uint8_t *d = map + (size_t)i * h->memmap_desc_size;
    uint32_t type = *(const uint32_t *)(const void *)d;
    uint64_t start = *(const uint64_t *)(const void *)(d + 8);
    uint64_t pages = *(const uint64_t *)(const void *)(d + 24);
    /* LoaderData holds copy_rsdp_low's retained RSDP page. Runtime code/data,
     * ACPI reclaim/NVS. No reclaimable boot
     * services or conventional RAM: those may belong to the allocator. */
    if (type != 2 && type != 5 && type != 6 && type != 9 && type != 10) continue;
    if (pages > UINT64_MAX / 4096 || addr < start) continue;
    uint64_t bytes = pages * 4096;
    if (addr - start <= bytes && length <= bytes - (addr - start)) {
      found = 1;
      break;
    }
  }
  if (!found) return NULL;
  for (uint64_t page = addr & ~4095ull; page <= (addr + length - 1) / 4096 * 4096;
       page += 4096) {
    if (!vmm_identity_is_writable(page)) return NULL;
  }
  return (const uint8_t *)(uintptr_t)addr;
}

void acpi_set_boot_context(const struct boot_handoff *handoff) {
  g_acpi_handoff = handoff;
  acpi_set_rsdp(handoff ? handoff->rsdp : 0);
}

static void efi_reset_system(uint32_t reset_type) {
  if (!g_efi_system_table) {
    return;
  }

  EFI_SYSTEM_TABLE_K *st = (EFI_SYSTEM_TABLE_K *)(uintptr_t)g_efi_system_table;
  if (!st || !st->RuntimeServices) {
    return;
  }

  EFI_RUNTIME_SERVICES_K *rt =
      (EFI_RUNTIME_SERVICES_K *)(uintptr_t)st->RuntimeServices;
  if (!rt || !rt->ResetSystem) {
    return;
  }

  rt->ResetSystem(reset_type, EFI_SUCCESS_K, 0, NULL);
}

void acpi_set_rsdp(uint64_t rsdp_addr) {
  g_rsdp_override = rsdp_addr;
  g_acpi_initialized = 0;
  g_power = (struct acpi_power_info){0};
  g_pm1a_cnt = g_pm1b_cnt = 0;
  g_reset_reg = (struct acpi_gas){0};
  g_reset_value = 0;
}

void acpi_set_uefi_system_table(uint64_t system_table_addr) {
  g_efi_system_table = system_table_addr;
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
          if (j >= length) return -1;
          uint8_t lead = dsdt[j];
          if ((lead & 0xC0) == 0) {
            j += 1; /* 1-byte length */
          } else {
            j += (lead >> 6) + 1; /* Multi-byte length */
          }
          j++; /* Skip NumElements */

          /* Now read the first element (SLP_TYPa) */
          if (j < length) {
            if (dsdt[j] == 0x0A && j + 1 < length && dsdt[j + 1] <= 7) { /* BytePrefix */
              *slp_typa = ((uint16_t)dsdt[j + 1]) << 10;
              return 0;
            } else if (dsdt[j] <= 1) { /* AML ZeroOp / OneOp */
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

  uint64_t previous_cr3 = vmm_enter_kernel_tables();
  if (acpi_tables_discover(g_rsdp_override, map_firmware, NULL, &g_power) != 0) {
    vmm_leave_kernel_tables(previous_cr3);
    return -1;
  }

  g_pm1a_cnt = g_power.pm1a_control;
  g_pm1b_cnt = g_power.pm1b_control;
  g_slp_typa = g_slp_typb = ACPI_SLP_TYP_S5;
  const uint8_t *dsdt = map_firmware(NULL, g_power.dsdt_address, g_power.dsdt_length);
  uint16_t slp_typ;
  if (dsdt && parse_s5_from_dsdt(dsdt + sizeof(struct acpi_header),
                                g_power.dsdt_length - sizeof(struct acpi_header),
                                &slp_typ) == 0) {
    g_slp_typa = g_slp_typb = slp_typ;
  }
  if (g_power.reset_port) {
    g_reset_reg.address_space_id = 1;
    g_reset_reg.address = g_power.reset_port;
    g_reset_value = g_power.reset_value;
  }

  vmm_leave_kernel_tables(previous_cr3);
  g_acpi_initialized = 1;
  return 0;
}

void acpi_shutdown(void) {
  efi_reset_system(EFI_RESET_SHUTDOWN_K);

  if (!g_acpi_initialized) {
    (void)acpi_init();
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
  efi_reset_system(EFI_RESET_COLD_K);

  if (!g_acpi_initialized) {
    (void)acpi_init();
  }

  __asm__ volatile("cli");

  /* Method 0: ACPI FADT Reset (Hyper-V preferred) */
  if (g_acpi_initialized &&
      g_reset_reg.address_space_id == 1 && g_reset_reg.address) { /* 1 = I/O Port */
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

int acpi_power_snapshot(struct acpi_power_info *out) {
  if (!out) return -1;
  *out = (struct acpi_power_info){0};
  if (acpi_init() != 0) return -1;
  *out = g_power;
  return 0;
}

int acpi_power_timer_read(uint32_t *value) {
  if (!value) return -1;
  *value = 0;
  if (acpi_init() != 0 || !g_power.timer_port) return -1;
  *value = inl(g_power.timer_port);
  if (g_power.timer_bits == 24) *value &= 0xffffffu;
  return 0;
}
