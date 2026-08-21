#include "boot/installer_input_policy.h"

#define INSTALLER_SERIAL_LSR_DATA_READY 0x01u
#define INSTALLER_SERIAL_LSR_RX_ERROR_MASK 0x1Eu
int installer_input_serial_probe_valid(uint8_t line_status, uint8_t byte,
                                       uint8_t expected_byte) {
  return line_status != 0xFFu &&
         (line_status & INSTALLER_SERIAL_LSR_DATA_READY) != 0u &&
         (line_status & INSTALLER_SERIAL_LSR_RX_ERROR_MASK) == 0u &&
         byte == expected_byte;
}

int installer_input_serial_key_valid(uint8_t line_status, uint8_t byte) {
  if (line_status == 0xFFu ||
      (line_status & INSTALLER_SERIAL_LSR_DATA_READY) == 0u ||
      (line_status & INSTALLER_SERIAL_LSR_RX_ERROR_MASK) != 0u) {
    return 0;
  }
  return byte == 0x08u || byte == '\n' || byte == '\r' ||
         (byte >= 0x20u && byte <= 0x7Eu);
}
