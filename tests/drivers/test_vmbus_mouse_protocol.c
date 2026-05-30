#include <stdio.h>

#include "drivers/hyperv/hyperv.h"

static int failures = 0;

static void fail(const char *msg) {
  printf("[vmbus_mouse_protocol] FAIL: %s\n", msg);
  failures++;
}

static void test_relative_report(void) {
  uint8_t raw[4] = {0x03u, 0x05u, 0xFEu, 0x01u};
  struct hyperv_mouse_report report;

  if (hyperv_mouse_parse_hid_report(raw, sizeof(raw), &report) != 1) {
    fail("relative report must parse");
    return;
  }
  if (report.absolute || report.buttons != 0x03u || report.dx != 5 ||
      report.dy != -2 || report.dz != 1) {
    fail("relative report fields drifted");
  }
}

static void test_absolute_report_with_report_id(void) {
  uint8_t raw[7] = {0x01u, 0x01u, 0x34u, 0x12u, 0x78u, 0x56u, 0xFFu};
  struct hyperv_mouse_report report;

  if (hyperv_mouse_parse_hid_report(raw, sizeof(raw), &report) != 1) {
    fail("absolute report must parse");
    return;
  }
  if (!report.absolute || report.buttons != 0x01u || report.abs_x != 0x1234u ||
      report.abs_y != 0x5678u || report.dz != -1) {
    fail("absolute report fields drifted");
  }
}

static void test_short_report_rejected(void) {
  uint8_t raw[2] = {0x01u, 0x02u};
  struct hyperv_mouse_report report;

  if (hyperv_mouse_parse_hid_report(raw, sizeof(raw), &report) != 0) {
    fail("short report must be rejected");
  }
}

int run_vmbus_mouse_protocol_tests(void) {
  failures = 0;
  test_relative_report();
  test_absolute_report_with_report_id();
  test_short_report_rejected();
  if (failures == 0) printf("[tests] vmbus_mouse_protocol OK\n");
  return failures;
}
