#ifndef CAPYOS_ACPI_TABLES_H
#define CAPYOS_ACPI_TABLES_H

#include <stddef.h>
#include <stdint.h>

/* Internal, allocation-free discovery. map must reject unreadable physical
 * spans; returned views remain valid for the duration of the call. */
typedef const uint8_t *(*acpi_table_map_fn)(void *, uint64_t, size_t);
#define ACPI_TABLE_MAX_BYTES (1024u * 1024u)
#define ACPI_ROOT_MAX_ENTRIES 256u
struct acpi_power_info {
  uint64_t dsdt_address;
  uint32_t dsdt_length;
  uint32_t flags;
  uint16_t pm1a_control, pm1b_control, timer_port, reset_port;
  uint8_t revision, uses_xsdt, profile, timer_bits, reset_value;
};

/* On any malformed required table, returns -1 and clears all output. An
 * advertised XSDT is authoritative: corruption never falls back to RSDT. */
int acpi_tables_discover(uint64_t rsdp, acpi_table_map_fn map, void *context,
                         struct acpi_power_info *out);
struct boot_handoff;
void acpi_set_boot_context(const struct boot_handoff *handoff);
int acpi_power_snapshot(struct acpi_power_info *out);
int acpi_power_timer_read(uint32_t *value);

uint32_t acpi_pm_timer_delta(uint32_t before, uint32_t after, uint8_t bits);

#endif
