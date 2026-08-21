#ifndef BOOT_INSTALLER_INPUT_POLICY_H
#define BOOT_INSTALLER_INPUT_POLICY_H

#include <stdint.h>

/* Pure policy shared by the UEFI loader and host regression tests. */
int installer_input_serial_probe_valid(uint8_t line_status, uint8_t byte,
                                       uint8_t expected_byte);
int installer_input_serial_key_valid(uint8_t line_status, uint8_t byte);

#endif
