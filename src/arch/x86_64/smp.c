#include "arch/x86_64/smp.h"
#include "arch/x86_64/apic.h"
#include "kernel/log/klog.h"
#include "memory/kmem.h"
#include <stddef.h>

/* ACPI MADT (Multiple APIC Description Table) parsing for CPU discovery */

#define ACPI_MADT_SIGNATURE 0x43495041 /* "APIC" */
#define MADT_TYPE_LOCAL_APIC 0
#define MADT_TYPE_IO_APIC    1
#define MADT_TYPE_OVERRIDE   2
#define MADT_TYPE_LOCAL_X2APIC 9

#define MADT_LAPIC_ENABLED   1

struct acpi_rsdp {
  char signature[8];
  uint8_t checksum;
  char oem_id[6];
  uint8_t revision;
  uint32_t rsdt_addr;
  uint32_t length;
  uint64_t xsdt_addr;
  uint8_t ext_checksum;
  uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
  uint32_t signature;
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  char oem_id[6];
  char oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
  struct acpi_sdt_header header;
  uint32_t local_apic_addr;
  uint32_t flags;
} __attribute__((packed));

struct madt_entry_header {
  uint8_t type;
  uint8_t length;
} __attribute__((packed));

struct madt_local_apic {
  struct madt_entry_header header;
  uint8_t processor_id;
  uint8_t apic_id;
  uint32_t flags;
} __attribute__((packed));

static struct smp_info g_smp;
static int g_smp_initialized = 0;

static void smp_memset(void *dst, int v, size_t n) {
  uint8_t *p = (uint8_t *)dst;
  for (size_t i = 0; i < n; i++) p[i] = (uint8_t)v;
}

void smp_init(void) {
  smp_memset(&g_smp, 0, sizeof(g_smp));
  g_smp_initialized = 0;
}

static struct acpi_sdt_header *find_table(uint64_t rsdp_addr, uint32_t sig) {
  if (!rsdp_addr) return NULL;

  struct acpi_rsdp *rsdp = (struct acpi_rsdp *)(uintptr_t)rsdp_addr;

  /* Try XSDT first (ACPI 2.0+) */
  if (rsdp->revision >= 2 && rsdp->xsdt_addr) {
    struct acpi_sdt_header *xsdt = (struct acpi_sdt_header *)(uintptr_t)rsdp->xsdt_addr;
    uint32_t entries = (xsdt->length - sizeof(*xsdt)) / 8;
    uint64_t *ptrs = (uint64_t *)((uint8_t *)xsdt + sizeof(*xsdt));
    for (uint32_t i = 0; i < entries; i++) {
      struct acpi_sdt_header *hdr = (struct acpi_sdt_header *)(uintptr_t)ptrs[i];
      if (hdr && hdr->signature == sig) return hdr;
    }
  }

  /* Fallback to RSDT */
  if (rsdp->rsdt_addr) {
    struct acpi_sdt_header *rsdt = (struct acpi_sdt_header *)(uintptr_t)rsdp->rsdt_addr;
    uint32_t entries = (rsdt->length - sizeof(*rsdt)) / 4;
    uint32_t *ptrs = (uint32_t *)((uint8_t *)rsdt + sizeof(*rsdt));
    for (uint32_t i = 0; i < entries; i++) {
      struct acpi_sdt_header *hdr = (struct acpi_sdt_header *)(uintptr_t)ptrs[i];
      if (hdr && hdr->signature == sig) return hdr;
    }
  }

  return NULL;
}

int smp_detect_cpus(uint64_t rsdp_addr) {
  smp_init();

  /* Get BSP APIC ID */
  uint32_t bsp_id = apic_id();
  g_smp.bsp_apic_id = bsp_id;

  struct acpi_sdt_header *madt_hdr = find_table(rsdp_addr, ACPI_MADT_SIGNATURE);
  if (!madt_hdr) {
    /* No MADT — assume single CPU (BSP only) */
    g_smp.cpus[0].apic_id = bsp_id;
    g_smp.cpus[0].cpu_index = 0;
    g_smp.cpus[0].is_bsp = 1;
    g_smp.cpus[0].state = CPU_STATE_ONLINE;
    g_smp.cpu_count = 1;
    g_smp.online_count = 1;
    g_smp_initialized = 1;
    klog(KLOG_INFO, "[smp] No MADT found. Single CPU mode.");
    return 1;
  }

  struct acpi_madt *madt = (struct acpi_madt *)madt_hdr;
  uint32_t madt_length = madt->header.length;
  uint8_t *entry_ptr = (uint8_t *)madt + sizeof(struct acpi_madt);
  uint8_t *end = (uint8_t *)madt + madt_length;

  uint32_t cpu_count = 0;

  while (entry_ptr < end && cpu_count < SMP_MAX_CPUS) {
    struct madt_entry_header *eh = (struct madt_entry_header *)entry_ptr;
    if (eh->length == 0) break;

    if (eh->type == MADT_TYPE_LOCAL_APIC) {
      struct madt_local_apic *lapic = (struct madt_local_apic *)entry_ptr;
      if (lapic->flags & MADT_LAPIC_ENABLED) {
        struct cpu_info *cpu = &g_smp.cpus[cpu_count];
        cpu->apic_id = lapic->apic_id;
        cpu->cpu_index = cpu_count;
        cpu->is_bsp = (lapic->apic_id == bsp_id) ? 1 : 0;
        cpu->state = cpu->is_bsp ? CPU_STATE_ONLINE : CPU_STATE_OFFLINE;
        cpu->kernel_stack = NULL;
        cpu->ticks = 0;
        cpu->current_task_pid = 0;
        cpu_count++;
      }
    }

    entry_ptr += eh->length;
  }

  if (cpu_count == 0) {
    /* No CPUs found in MADT — add BSP manually */
    g_smp.cpus[0].apic_id = bsp_id;
    g_smp.cpus[0].cpu_index = 0;
    g_smp.cpus[0].is_bsp = 1;
    g_smp.cpus[0].state = CPU_STATE_ONLINE;
    cpu_count = 1;
  }

  g_smp.cpu_count = cpu_count;
  g_smp.online_count = 1; /* Only BSP is online initially */
  g_smp_initialized = 1;

  klog(KLOG_INFO, "[smp] Detected CPUs via MADT.");
  klog_dec(KLOG_INFO, "[smp] CPU count: ", cpu_count);
  klog_dec(KLOG_INFO, "[smp] BSP APIC ID: ", bsp_id);

  return (int)cpu_count;
}

int smp_start_aps(void) {
  if (!g_smp_initialized || g_smp.cpu_count <= 1) return 0;

  int started = 0;

  for (uint32_t i = 0; i < g_smp.cpu_count; i++) {
    struct cpu_info *cpu = &g_smp.cpus[i];
    if (cpu->is_bsp || cpu->state != CPU_STATE_OFFLINE) continue;

    /* Allocate kernel stack for this AP */
    cpu->kernel_stack = (uint64_t *)kmalloc(16384);
    if (!cpu->kernel_stack) continue;

    cpu->state = CPU_STATE_STARTING;

    /* Send INIT IPI */
    volatile uint32_t *icr_lo = (volatile uint32_t *)(uintptr_t)(apic_base_address() + 0x300);
    volatile uint32_t *icr_hi = (volatile uint32_t *)(uintptr_t)(apic_base_address() + 0x310);

    /* INIT IPI: delivery=INIT, level=assert, target=apic_id */
    *icr_hi = ((uint32_t)cpu->apic_id) << 24;
    *icr_lo = 0x00004500; /* INIT, level assert */

    /* Delay ~10ms */
    for (volatile int d = 0; d < 1000000; d++) {}

    /* Deassert */
    *icr_hi = ((uint32_t)cpu->apic_id) << 24;
    *icr_lo = 0x00008500; /* INIT, level deassert */

    for (volatile int d = 0; d < 100000; d++) {}

    /* STARTUP IPI: vector = trampoline page (0x08 = physical addr 0x8000) */
    /* The AP will start executing at physical address vector*4096 = 0x8000 */
    *icr_hi = ((uint32_t)cpu->apic_id) << 24;
    *icr_lo = 0x00004608; /* STARTUP, vector=0x08 → addr 0x8000 */

    for (volatile int d = 0; d < 200000; d++) {}

    /* Send STARTUP IPI again (spec recommends two) */
    *icr_hi = ((uint32_t)cpu->apic_id) << 24;
    *icr_lo = 0x00004608;

    /* Wait for AP to come online */
    for (volatile int d = 0; d < 5000000; d++) {
      if (cpu->state == CPU_STATE_ONLINE) break;
    }

    if (cpu->state == CPU_STATE_ONLINE) {
      started++;
      g_smp.online_count++;
      klog_dec(KLOG_INFO, "[smp] AP online, APIC ID: ", cpu->apic_id);
    } else {
      cpu->state = CPU_STATE_HALTED;
      klog_dec(KLOG_WARN, "[smp] AP failed to start, APIC ID: ", cpu->apic_id);
    }
  }

  klog_dec(KLOG_INFO, "[smp] APs started: ", (uint32_t)started);
  return started;
}

uint32_t smp_cpu_count(void) {
  return g_smp_initialized ? g_smp.cpu_count : 1;
}

uint32_t smp_online_count(void) {
  return g_smp_initialized ? g_smp.online_count : 1;
}

struct cpu_info *smp_current_cpu(void) {
  if (!g_smp_initialized) return &g_smp.cpus[0];
  uint32_t id = apic_id();
  for (uint32_t i = 0; i < g_smp.cpu_count; i++) {
    if (g_smp.cpus[i].apic_id == id) return &g_smp.cpus[i];
  }
  return &g_smp.cpus[0];
}

struct cpu_info *smp_cpu_at(uint32_t index) {
  if (index >= g_smp.cpu_count) return NULL;
  return &g_smp.cpus[index];
}

void smp_get_info(struct smp_info *out) {
  if (out) *out = g_smp;
}

int smp_available(void) {
  return g_smp_initialized;
}
