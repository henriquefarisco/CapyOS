#include "drivers/acpi/acpi_tables.h"
#include "drivers/acpi/acpi.h"
#include "boot/handoff.h"
#include <stdio.h>
#include <string.h>

static uint8_t memory[16384] __attribute__((aligned(4096)));
static uint64_t base;
static unsigned reads;
static int mapped = 1, entered, left;
/* Runtime discovery is tested with real driver code, replacing only page-table
 * queries. No test calls I/O port, firmware reset or sleep instructions. */
int vmm_identity_is_writable(uint64_t addr) { (void)addr; return mapped; }
uint64_t vmm_enter_kernel_tables(void) { ++entered; return 123; }
void vmm_leave_kernel_tables(uint64_t previous) { if (previous == 123) ++left; }
static const uint8_t *map(void *ctx, uint64_t addr, size_t n) {
  (void)ctx;
  ++reads;
  if (addr < base || addr - base > sizeof(memory) ||
      n > sizeof(memory) - (size_t)(addr - base)) return NULL;
  return memory + (size_t)(addr - base);
}
static void put32(uint8_t *p, uint32_t x) {
  for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (8 * i));
}
static void put64(uint8_t *p, uint64_t x) {
  put32(p, (uint32_t)x); put32(p + 4, (uint32_t)(x >> 32));
}
static void sum(uint8_t *p, unsigned n, unsigned offset) {
  uint8_t v = 0;
  p[offset] = 0;
  for (unsigned i = 0; i < n; ++i) v = (uint8_t)(v + p[i]);
  p[offset] = (uint8_t)(0 - v);
}
static void header(unsigned offset, const char *sig, unsigned n) {
  memcpy(memory + offset, sig, 4);
  put32(memory + offset + 4, n);
  memory[offset + 8] = 6;
}
static void fixture(int xsdt, uint64_t start) {
  memset(memory, 0, sizeof(memory));
  base = start; reads = 0;
  memcpy(memory, "RSD PTR ", 8);
  memory[15] = xsdt ? 2 : 0;
  put32(memory + 16, (uint32_t)(base + 256));
  put32(memory + 20, 36);
  put64(memory + 24, xsdt ? base + 256 : 0);
  sum(memory, 20, 8); sum(memory, 36, 32);
  header(256, xsdt ? "XSDT" : "RSDT", xsdt ? 44 : 40);
  if (xsdt) put64(memory + 292, base + 512);
  else put32(memory + 292, (uint32_t)(base + 512));
  sum(memory + 256, xsdt ? 44 : 40, 9);
  header(512, "FACP", 244);
  uint8_t *f = memory + 512;
  put32(f + 40, (uint32_t)(base + 1024));
  put64(f + 140, xsdt ? base + 1024 : 0);
  f[45] = 2; f[89] = 2; f[91] = 4;
  put32(f + 64, 0x404); put32(f + 76, 0x408);
  put32(f + 112, 1u << 10);
  f[116] = 1; f[117] = 8; f[119] = 1;
  put64(f + 120, 0xcf9); f[128] = 6;
  sum(f, 244, 9);
  header(1024, "DSDT", 36); sum(memory + 1024, 36, 9);
}
#define CHECK(x) do { ++checks; if (!(x)) { \
  printf("[acpi-tables] FAIL line %d: %s\n", __LINE__, #x); ++failures; \
} } while (0)
static int rejected_and_cleared(struct acpi_power_info *info) {
  memset(info, 0xa5, sizeof(*info));
  struct acpi_power_info empty = {0};
  int rc = acpi_tables_discover(base, map, NULL, info);
  return rc == -1 && memcmp(info, &empty, sizeof(empty)) == 0;
}
int run_acpi_tables_tests(void) {
  int checks = 0, failures = 0;
  struct acpi_power_info info;
  for (int extended = 0; extended < 2; ++extended) {
    fixture(extended, extended ? 0x100000000ull : 4096);
    CHECK(acpi_tables_discover(base, map, NULL, &info) == 0);
    CHECK(info.uses_xsdt == extended && info.profile == 2);
    CHECK(info.dsdt_address == base + 1024 && info.dsdt_length == 36);
    CHECK(info.pm1a_control == 0x404 && info.pm1b_control == 0);
    CHECK(info.timer_port == 0x408 && info.timer_bits == 24);
    CHECK(info.reset_port == 0xcf9 && info.reset_value == 6);
  }
  /* Every byte truncation of the required tables must fail closed. */
  const unsigned offsets[] = {256, 512, 1024};
  const unsigned lengths[] = {44, 244, 36};
  for (unsigned t = 0; t < 3; ++t) {
    for (unsigned n = 0; n < lengths[t]; ++n) {
      fixture(1, 4096);
      /* FADT is allowed to omit optional extensions, but not its v1 prefix. */
      if (t == 1 && n >= 116) continue;
      put32(memory + offsets[t] + 4, n);
      if (n >= 10) sum(memory + offsets[t], n, 9);
      CHECK(rejected_and_cleared(&info));
    }
  }
  for (unsigned t = 0; t < 3; ++t) {
    fixture(1, 4096); memory[offsets[t] + 9] ^= 1;
    CHECK(rejected_and_cleared(&info));
    fixture(1, 4096); put32(memory + offsets[t] + 4, 0xffffffffu);
    CHECK(rejected_and_cleared(&info));
  }
  fixture(1, 4096); memory[8] ^= 1; CHECK(rejected_and_cleared(&info));
  fixture(1, 4096); memory[32] ^= 1; CHECK(rejected_and_cleared(&info));
  fixture(1, 4096); put32(memory + 20, 4097); sum(memory, 36, 32);
  CHECK(rejected_and_cleared(&info));
  fixture(1, 4096); put64(memory + 24, UINT64_MAX - 16); sum(memory, 36, 32);
  CHECK(rejected_and_cleared(&info)); /* never falls back to good RSDT */
  fixture(1, 4096); put32(memory + 260, 45); sum(memory + 256, 45, 9);
  CHECK(rejected_and_cleared(&info)); /* partial root entry */
  fixture(1, 4096); put32(memory + 260, 52); put64(memory + 300, base + 512);
  sum(memory + 256, 52, 9); CHECK(rejected_and_cleared(&info));
  fixture(1, 4096); put32(memory + 260, 36 + 257 * 8);
  sum(memory + 256, 36 + 257 * 8, 9); CHECK(rejected_and_cleared(&info));
  CHECK(reads <= 6); /* reject oversized root before traversal */
  fixture(1, 4096); put64(memory + 292, base + sizeof(memory) - 8);
  sum(memory + 256, 44, 9); CHECK(rejected_and_cleared(&info));
  fixture(1, 4096); put64(memory + 512 + 140, UINT64_MAX);
  sum(memory + 512, 244, 9); CHECK(rejected_and_cleared(&info));
  /* Unsupported/overflowing register encodings must never become I/O ports. */
  for (unsigned which = 0; which < 7; ++which) {
    fixture(1, 4096); uint8_t *f = memory + 512;
    f[208] = 1; f[209] = 32; f[211] = 3; put64(f + 212, 0x508);
    if (which == 0) f[208] = 0; /* MMIO */
    if (which == 1) f[209] = 24;
    if (which == 2) f[210] = 1;
    if (which == 3) f[211] = 2;
    if (which == 4) put64(f + 212, 0x100000508ull);
    if (which == 5) put64(f + 212, 0xffff);
    if (which == 6) f[91] = 0;
    sum(f, 244, 9);
    CHECK(acpi_tables_discover(base, map, NULL, &info) == 0);
    CHECK(info.timer_port == 0 && info.timer_bits == 0);
  }
  fixture(1, 4096); uint8_t *f = memory + 512;
  f[208] = 1; f[209] = 32; f[211] = 3; put64(f + 212, 0x508);
  put32(f + 112, 1u << 8); sum(f, 244, 9);
  CHECK(acpi_tables_discover(base, map, NULL, &info) == 0);
  CHECK(info.timer_port == 0x508 && info.timer_bits == 32 && !info.reset_port);
  put32(f + 112, 1u << 20); sum(f, 244, 9);
  CHECK(acpi_tables_discover(base, map, NULL, &info) == 0);
  CHECK(!info.timer_port && !info.pm1a_control);
  CHECK(acpi_pm_timer_delta(0xfffffe, 2, 24) == 4);
  CHECK(acpi_pm_timer_delta(0xfffffffe, 2, 32) == 4);
  CHECK(acpi_pm_timer_delta(1, 2, 0) == 0);
  CHECK(acpi_tables_discover(base, NULL, NULL, &info) == -1);
  CHECK(acpi_tables_discover(base, map, NULL, NULL) == -1);
  /* Real driver: firmware-memory policy, mapping failure, cleared snapshots,
   * context replacement and CR3 restoration on success and failure. */
  uint64_t descriptor[5] = {9, (uint64_t)(uintptr_t)memory, 0, 4, 0};
  struct boot_handoff handoff = {0};
  handoff.rsdp = descriptor[1];
  handoff.memmap = (uint64_t)(uintptr_t)descriptor;
  handoff.memmap_desc_size = sizeof(descriptor);
  handoff.memmap_size = sizeof(descriptor);
  handoff.memmap_entries = 1;
  fixture(1, descriptor[1]);
  acpi_set_boot_context(&handoff);
  CHECK(acpi_power_snapshot(&info) == 0 && info.timer_port == 0x408);
  CHECK(entered == left);
  descriptor[0] = 2; /* retained low RSDP copy allocated by the UEFI loader */
  acpi_set_boot_context(&handoff);
  CHECK(acpi_power_snapshot(&info) == 0 && entered == left);
  for (unsigned mode = 0; mode < 5; ++mode) {
    descriptor[0] = mode == 0 ? 7 : mode == 1 ? 11 : 9;
    descriptor[3] = mode == 2 ? 0 : 4;
    mapped = mode != 3;
    handoff.memmap_size = mode == 4 ? 39 : sizeof(descriptor);
    acpi_set_boot_context(&handoff);
    memset(&info, 0xa5, sizeof(info));
    CHECK(acpi_power_snapshot(&info) == -1 && !info.timer_port && !info.dsdt_address);
    CHECK(!acpi_is_available() && entered == left);
  }
  acpi_set_boot_context(NULL);
  CHECK(acpi_power_snapshot(&info) == -1 && entered == left);
  mapped = 1; handoff.memmap_size = sizeof(descriptor);
  descriptor[0] = 9; descriptor[1] = 0xfffffffffffff000ull;
  descriptor[3] = 1; handoff.rsdp = descriptor[1];
  acpi_set_boot_context(&handoff);
  CHECK(acpi_power_snapshot(&info) == -1 && entered == left);
  printf("[acpi-tables] %d checks, %d failures\n", checks, failures);
  return failures;
}
#ifdef ACPI_TABLES_TEST_MAIN
int main(void) { return run_acpi_tables_tests() ? 1 : 0; }
#endif
