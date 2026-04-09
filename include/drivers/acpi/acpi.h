/**
 * ACPI Driver for CAPYOS
 * Provides power management via ACPI tables.
 */
#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

/* RSDP (Root System Description Pointer) */
struct acpi_rsdp {
  char signature[8]; /* "RSD PTR " */
  uint8_t checksum;
  char oemid[6];
  uint8_t revision;
  uint32_t rsdt_address;
  /* ACPI 2.0+ fields */
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t extended_checksum;
  uint8_t reserved[3];
} __attribute__((packed));

/* Generic Address Structure (GAS) */
struct acpi_gas {
  uint8_t address_space_id;
  uint8_t register_bit_width;
  uint8_t register_bit_offset;
  uint8_t access_size;
  uint64_t address;
} __attribute__((packed));

/* Generic ACPI table header */
struct acpi_header {
  char signature[4];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  char oemid[6];
  char oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} __attribute__((packed));

/* FADT (Fixed ACPI Description Table) - simplified */
struct acpi_fadt {
  struct acpi_header header;
  uint32_t firmware_ctrl;
  uint32_t dsdt;
  uint8_t reserved1;
  uint8_t preferred_pm_profile;
  uint16_t sci_interrupt;
  uint32_t smi_command;
  uint8_t acpi_enable;
  uint8_t acpi_disable;
  uint8_t s4bios_req;
  uint8_t pstate_control;
  uint32_t pm1a_event_block;
  uint32_t pm1b_event_block;
  uint32_t pm1a_control_block; /* PM1a_CNT address */
  uint32_t pm1b_control_block; /* PM1b_CNT address */
  uint32_t pm2_control_block;
  uint32_t pm_timer_block;
  uint32_t gpe0_block;
  uint32_t gpe1_block;
  uint8_t pm1_event_length;
  uint8_t pm1_control_length;
  uint8_t pm2_control_length;
  uint8_t pm_timer_length;
  uint8_t gpe0__block_length;
  uint8_t gpe1_block_length;
  uint8_t gpe1_base;
  uint8_t cst_cnt;
  uint16_t p_lvl2_lat;
  uint16_t p_lvl3_lat;
  uint16_t flush_size;
  uint16_t flush_stride;
  uint8_t duty_offset;
  uint8_t duty_width;
  uint8_t day_alrm;
  uint8_t mon_alrm;
  uint8_t century;
  uint16_t iapc_boot_arch;
  uint8_t reserved2;
  uint32_t flags;
  struct acpi_gas reset_reg;
  uint8_t reset_value;
  uint8_t reserved3[3];
  uint64_t x_firmware_ctrl;
  uint64_t x_dsdt;
  struct acpi_gas x_pm1a_event_block;
  struct acpi_gas x_pm1b_event_block;
  struct acpi_gas x_pm1a_control_block;
  struct acpi_gas x_pm1b_control_block;
  struct acpi_gas x_pm2_control_block;
  struct acpi_gas x_pm_timer_block;
  struct acpi_gas x_gpe0_block;
  struct acpi_gas x_gpe1_block;
  struct acpi_gas sleep_control_reg;
  struct acpi_gas sleep_status_reg;
  uint64_t hypervisor_vendor_identity;
} __attribute__((packed));

void acpi_set_rsdp(uint64_t rsdp_addr);
void acpi_set_uefi_system_table(uint64_t system_table_addr);

/**
 * Initialize ACPI subsystem.
 * Finds and parses ACPI tables.
 * Returns 0 on success, -1 on failure.
 */
int acpi_init(void);

/**
 * Shutdown the system via ACPI S5 state.
 * Does not return on success.
 */
void acpi_shutdown(void);

/**
 * Reboot the system via ACPI reset register.
 * Falls back to keyboard controller if ACPI reset unavailable.
 * Does not return on success.
 */
void acpi_reboot(void);

/**
 * Check if ACPI is available and initialized.
 */
int acpi_is_available(void);

#endif /* ACPI_H */
