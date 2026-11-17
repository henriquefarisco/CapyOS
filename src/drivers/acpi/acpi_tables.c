#include "drivers/acpi/acpi_tables.h"

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t le64(const uint8_t *p) {
  return le32(p) | (uint64_t)le32(p + 4) << 32;
}
static int equal(const uint8_t *p, const char *s, size_t n) {
  for (size_t i = 0; i < n; i++) if (p[i] != (uint8_t)s[i]) return 0;
  return 1;
}
static int checksum(const uint8_t *p, size_t n) {
  uint8_t sum = 0;
  for (size_t i = 0; i < n; i++) sum = (uint8_t)(sum + p[i]);
  return sum == 0;
}
static const uint8_t *view(acpi_table_map_fn map, void *ctx,
                           uint64_t addr, size_t n) {
  if (!addr || !n || addr > UINT64_MAX - (n - 1u)) return NULL;
  return map(ctx, addr, n);
}
static const uint8_t *table(acpi_table_map_fn map, void *ctx,
                            uint64_t addr, const char *signature) {
  const uint8_t *p = view(map, ctx, addr, 36);
  if (!p || !equal(p, signature, 4)) return NULL;
  uint32_t n = le32(p + 4);
  if (n < 36 || n > ACPI_TABLE_MAX_BYTES) return NULL;
  p = view(map, ctx, addr, n);
  return p && checksum(p, n) ? p : NULL;
}
/* Only SystemIO is supported in this slice. Never truncate addresses or
 * silently fall back from an advertised unsupported extended register. */
static uint16_t io_register(const uint8_t *f, uint32_t length,
                             size_t legacy, size_t extended, unsigned bytes) {
  uint64_t address = le32(f + legacy);
  if (length >= extended + 12 && le64(f + extended + 4)) {
    const uint8_t *g = f + extended;
    if (g[0] != 1 || g[1] != bytes * 8 || g[2] != 0 ||
        (g[3] != 0 && g[3] != (bytes == 2 ? 2 : 3))) return 0;
    address = le64(g + 4);
  }
  return address && address <= 0x10000u - bytes ? (uint16_t)address : 0;
}

int acpi_tables_discover(uint64_t rsdp, acpi_table_map_fn map, void *ctx,
                         struct acpi_power_info *out) {
  struct acpi_power_info info = {0};
  if (!out) return -1;
  *out = info;
  if (!map) return -1;
  const uint8_t *r = view(map, ctx, rsdp, 20);
  if (!r || !equal(r, "RSD PTR ", 8) || !checksum(r, 20)) return -1;
  info.revision = r[15];
  uint64_t root = le32(r + 16);
  unsigned stride = 4;
  if (info.revision >= 2) {
    r = view(map, ctx, rsdp, 36);
    if (!r) return -1;
    uint32_t n = le32(r + 20);
    if (n < 36 || n > 4096) return -1;
    r = view(map, ctx, rsdp, n);
    if (!r || !checksum(r, n)) return -1;
    if (le64(r + 24)) {
      root = le64(r + 24);
      stride = 8;
      info.uses_xsdt = 1;
    }
  }
  r = table(map, ctx, root, stride == 8 ? "XSDT" : "RSDT");
  if (!r) return -1;
  uint32_t payload = le32(r + 4) - 36;
  if (payload % stride || payload / stride > ACPI_ROOT_MAX_ENTRIES) return -1;
  const uint8_t *f = NULL;
  for (size_t i = 0; i < payload; i += stride) {
    uint64_t addr = stride == 8 ? le64(r + 36 + i) : le32(r + 36 + i);
    const uint8_t *h = view(map, ctx, addr, 36);
    if (!h) return -1;
    if (!equal(h, "FACP", 4)) continue;
    if (f) return -1; /* ambiguous duplicate FADT */
    f = table(map, ctx, addr, "FACP");
    if (!f) return -1;
  }
  if (!f || le32(f + 4) < 116) return -1;
  uint32_t n = le32(f + 4);
  info.flags = le32(f + 112);
  info.profile = f[45];
  info.dsdt_address = le32(f + 40);
  if (n >= 148 && le64(f + 140)) info.dsdt_address = le64(f + 140);
  const uint8_t *d = table(map, ctx, info.dsdt_address, "DSDT");
  if (!d) return -1;
  info.dsdt_length = le32(d + 4);
  if (!(info.flags & (1u << 20))) { /* HW_REDUCED_ACPI */
    if (f[89] == 2) {
      info.pm1a_control = io_register(f, n, 64, 172, 2);
      info.pm1b_control = io_register(f, n, 68, 184, 2);
    }
    if (f[91] == 4) {
      info.timer_port = io_register(f, n, 76, 208, 4);
      if (info.timer_port) info.timer_bits = info.flags & (1u << 8) ? 32 : 24;
    }
  }
  if (n >= 129 && (info.flags & (1u << 10)) &&
      f[116] == 1 && f[117] == 8 && f[118] == 0 &&
      (f[119] == 0 || f[119] == 1) &&
      le64(f + 120) && le64(f + 120) <= 0xffffu) {
    info.reset_port = (uint16_t)le64(f + 120);
    info.reset_value = f[128];
  }
  *out = info;
  return 0;
}

uint32_t acpi_pm_timer_delta(uint32_t before, uint32_t after, uint8_t bits) {
  if (bits == 24) return (after - before) & 0xffffffu;
  return bits == 32 ? after - before : 0;
}
