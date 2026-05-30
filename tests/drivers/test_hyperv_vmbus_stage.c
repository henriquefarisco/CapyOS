#include "drivers/hyperv/hyperv.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_hyperv_vmbus_failures = 0;
static int g_hyperv_vmbus_total = 0;

#define EXPECT(cond, msg)                                                    \
  do {                                                                       \
    ++g_hyperv_vmbus_total;                                                  \
    if (!(cond)) {                                                           \
      ++g_hyperv_vmbus_failures;                                             \
      fprintf(stderr, "[fail] %s:%d EXPECT(%s) - %s\n", __FILE__, __LINE__, \
              #cond, (msg));                                                 \
    }                                                                        \
  } while (0)

static void test_stage_labels_are_stable(void) {
  EXPECT(strcmp(hyperv_vmbus_stage_label(HYPERV_VMBUS_STAGE_OFF), "off") == 0,
         "off label");
  EXPECT(strcmp(hyperv_vmbus_stage_label(HYPERV_VMBUS_STAGE_HYPERCALL),
                "hypercall") == 0,
         "hypercall label");
  EXPECT(strcmp(hyperv_vmbus_stage_label(HYPERV_VMBUS_STAGE_SYNIC),
                "synic") == 0,
         "synic label");
  EXPECT(strcmp(hyperv_vmbus_stage_label(HYPERV_VMBUS_STAGE_CONTACT),
                "contact") == 0,
         "contact label");
  EXPECT(strcmp(hyperv_vmbus_stage_label(HYPERV_VMBUS_STAGE_OFFERS),
                "offers") == 0,
         "offers label");
  EXPECT(strcmp(hyperv_vmbus_stage_label(HYPERV_VMBUS_STAGE_CHANNEL),
                "channel") == 0,
         "channel label");
  EXPECT(strcmp(hyperv_vmbus_stage_label(HYPERV_VMBUS_STAGE_CONTROL),
                "control") == 0,
         "control label");
  EXPECT(strcmp(hyperv_vmbus_stage_label(HYPERV_VMBUS_STAGE_READY),
                "ready") == 0,
         "ready label");
  EXPECT(strcmp(hyperv_vmbus_stage_label(HYPERV_VMBUS_STAGE_FAILED),
                "failed") == 0,
         "failed label");
  EXPECT(strcmp(hyperv_vmbus_stage_label(0xFFu), "off") == 0,
         "unknown label defaults to off");
}

static void test_runtime_stage_progression(void) {
  EXPECT(hyperv_runtime_stage_for(HYPERV_VMBUS_STAGE_SYNIC, 0u, 0u, 0u, 0u, 0) ==
             HYPERV_VMBUS_STAGE_SYNIC,
         "unconfigured runtime reports vmbus stage");
  EXPECT(hyperv_runtime_stage_for(HYPERV_VMBUS_STAGE_CONTACT, 1u, 1u, 0u, 0u, 0) ==
             HYPERV_VMBUS_STAGE_OFFERS,
         "cached offer promotes to offers");
  EXPECT(hyperv_runtime_stage_for(HYPERV_VMBUS_STAGE_CONTACT, 1u, 0u, 1u, 0u, 0) ==
             HYPERV_VMBUS_STAGE_CHANNEL,
         "ready channel promotes to channel");
  EXPECT(hyperv_runtime_stage_for(HYPERV_VMBUS_STAGE_CONTACT, 1u, 0u, 0u, 4u, 0) ==
             HYPERV_VMBUS_STAGE_CONTROL,
         "runtime phase control wins");
  EXPECT(hyperv_runtime_stage_for(HYPERV_VMBUS_STAGE_CONTACT, 1u, 0u, 0u, 5u, 0) ==
             HYPERV_VMBUS_STAGE_READY,
         "runtime phase ready wins");
  EXPECT(hyperv_runtime_stage_for(HYPERV_VMBUS_STAGE_READY, 1u, 1u, 1u, 6u, 0) ==
             HYPERV_VMBUS_STAGE_FAILED,
         "failed phase wins over ready inputs");
  EXPECT(hyperv_runtime_stage_for(HYPERV_VMBUS_STAGE_READY, 1u, 1u, 1u, 5u, -1) ==
             HYPERV_VMBUS_STAGE_FAILED,
         "negative error wins over ready inputs");
}

static void test_msg_conn_id_sanitization(void) {
  EXPECT(hyperv_vmbus_sanitize_msg_conn_id(HYPERV_VMBUS_VERSION_WIN10, 0u,
                                           HYPERV_VMBUS_MESSAGE_CONNECTION_ID_4) ==
             HYPERV_VMBUS_MESSAGE_CONNECTION_ID_4,
         "zero response uses fallback");
  EXPECT(hyperv_vmbus_sanitize_msg_conn_id(HYPERV_VMBUS_VERSION_WIN10,
                                           HYPERV_VMBUS_VERSION_WIN10,
                                           HYPERV_VMBUS_MESSAGE_CONNECTION_ID_4) ==
             HYPERV_VMBUS_MESSAGE_CONNECTION_ID_4,
         "version echo is suspicious on Win10 protocol");
  EXPECT(hyperv_vmbus_sanitize_msg_conn_id(HYPERV_VMBUS_VERSION_WIN10, 0x100u,
                                           HYPERV_VMBUS_MESSAGE_CONNECTION_ID_4) ==
             HYPERV_VMBUS_MESSAGE_CONNECTION_ID_4,
         "large Win10 connection id uses fallback");
  EXPECT(hyperv_vmbus_sanitize_msg_conn_id(HYPERV_VMBUS_VERSION_WIN10, 4u,
                                           HYPERV_VMBUS_MESSAGE_CONNECTION_ID) == 4u,
         "small explicit id is accepted");
  EXPECT(hyperv_vmbus_sanitize_msg_conn_id(0x00020000u, 0x100u,
                                           HYPERV_VMBUS_MESSAGE_CONNECTION_ID) ==
             0x100u,
         "legacy protocol keeps host-provided id");
}

int run_hyperv_vmbus_stage_tests(void) {
  test_stage_labels_are_stable();
  test_runtime_stage_progression();
  test_msg_conn_id_sanitization();
  fprintf(stderr, "hyperv_vmbus_stage: %d/%d tests passed (%d failures)\n",
          g_hyperv_vmbus_total - g_hyperv_vmbus_failures,
          g_hyperv_vmbus_total, g_hyperv_vmbus_failures);
  return g_hyperv_vmbus_failures == 0 ? 0 : 1;
}
