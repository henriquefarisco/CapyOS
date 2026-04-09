#include "drivers/hyperv/hyperv.h"

const char *hyperv_vmbus_stage_label(uint8_t stage) {
  switch (stage) {
  case HYPERV_VMBUS_STAGE_HYPERCALL:
    return "hypercall";
  case HYPERV_VMBUS_STAGE_SYNIC:
    return "synic";
  case HYPERV_VMBUS_STAGE_CONTACT:
    return "contact";
  case HYPERV_VMBUS_STAGE_OFFERS:
    return "offers";
  case HYPERV_VMBUS_STAGE_CHANNEL:
    return "channel";
  case HYPERV_VMBUS_STAGE_CONTROL:
    return "control";
  case HYPERV_VMBUS_STAGE_READY:
    return "ready";
  case HYPERV_VMBUS_STAGE_FAILED:
    return "failed";
  default:
    return "off";
  }
}

uint8_t hyperv_runtime_stage_for(uint8_t vmbus_stage, uint8_t configured,
                                 uint8_t offer_ready, uint8_t channel_ready,
                                 uint8_t runtime_phase, int32_t last_error) {
  if (last_error < 0 || runtime_phase == 6u) {
    return HYPERV_VMBUS_STAGE_FAILED;
  }
  if (!configured) {
    return vmbus_stage;
  }
  if (runtime_phase == 5u) {
    return HYPERV_VMBUS_STAGE_READY;
  }
  if (runtime_phase == 4u) {
    return HYPERV_VMBUS_STAGE_CONTROL;
  }
  if (runtime_phase == 3u || channel_ready) {
    return HYPERV_VMBUS_STAGE_CHANNEL;
  }
  if (runtime_phase == 2u || offer_ready) {
    return HYPERV_VMBUS_STAGE_OFFERS;
  }
  return vmbus_stage;
}
