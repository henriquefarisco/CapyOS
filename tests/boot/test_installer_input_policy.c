#include "boot/installer_input_policy.h"

#include <stdint.h>
#include <stdio.h>

static int expect_int(int actual, int expected, const char *name) {
  if (actual != expected) {
    printf("[FAIL] installer_input_policy: %s (got %d, expected %d)\n",
           name, actual, expected);
    return 1;
  }
  return 0;
}

static int test_serial_probe_policy(void) {
  int fails = 0;
  fails += expect_int(installer_input_serial_probe_valid(0x61u, 0xAEu, 0xAEu),
                      1, "exact loopback response accepted");
  fails += expect_int(installer_input_serial_probe_valid(0x60u, 0xAEu, 0xAEu),
                      0, "loopback byte rejected without data-ready");
  fails += expect_int(installer_input_serial_probe_valid(0x63u, 0xAEu, 0xAEu),
                      0, "loopback response with receive error rejected");
  fails += expect_int(installer_input_serial_probe_valid(0xFFu, 0xAEu, 0xAEu),
                      0, "absent UART status rejected");
  fails += expect_int(installer_input_serial_probe_valid(0x61u, 0xAFu, 0xAEu),
                      0, "mismatched loopback byte rejected");
  return fails;
}

static int test_serial_key_policy(void) {
  int fails = 0;
  fails += expect_int(installer_input_serial_key_valid(0x61u, '1'), 1,
                      "printable ASCII accepted");
  fails += expect_int(installer_input_serial_key_valid(0x61u, '\r'), 1,
                      "carriage return accepted");
  fails += expect_int(installer_input_serial_key_valid(0x61u, '\n'), 1,
                      "line feed accepted");
  fails += expect_int(installer_input_serial_key_valid(0x61u, 0x08u), 1,
                      "backspace accepted");
  fails += expect_int(installer_input_serial_key_valid(0xFFu, 0xFFu), 0,
                      "absent UART 0xFF event rejected");
  fails += expect_int(installer_input_serial_key_valid(0x61u, 0xFFu), 0,
                      "non-ASCII byte rejected");
  fails += expect_int(installer_input_serial_key_valid(0x60u, '1'), 0,
                      "byte rejected without data-ready");
  fails += expect_int(installer_input_serial_key_valid(0x63u, '1'), 0,
                      "receive error rejected");
  fails += expect_int(installer_input_serial_key_valid(0x61u, 0x1Bu), 0,
                      "escape control byte rejected");
  fails += expect_int(installer_input_serial_key_valid(0x61u, 0x7Fu), 0,
                      "delete control byte rejected");
  return fails;
}

int run_installer_input_policy_tests(void) {
  int fails = 0;
  fails += test_serial_probe_policy();
  fails += test_serial_key_policy();
  if (fails == 0) {
    printf("[OK] installer_input_policy\n");
  }
  return fails;
}

#if defined(INSTALLER_INPUT_STANDALONE)
int main(void) {
  return run_installer_input_policy_tests() == 0 ? 0 : 1;
}
#endif
