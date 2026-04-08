#ifndef DRIVERS_STORAGE_STORVSC_RUNTIME_H
#define DRIVERS_STORAGE_STORVSC_RUNTIME_H

#include "drivers/storage/storvsc_backend.h"

#include <stdint.h>

enum storvsc_runtime_phase {
  STORVSC_RUNTIME_UNCONFIGURED = 0,
  STORVSC_RUNTIME_DISABLED,
  STORVSC_RUNTIME_PROBE,
  STORVSC_RUNTIME_CHANNEL,
  STORVSC_RUNTIME_CONTROL,
  STORVSC_RUNTIME_READY,
  STORVSC_RUNTIME_FAILED,
};

struct storvsc_controller_status {
  uint8_t configured;
  uint8_t enabled;
  uint8_t ready;
  uint8_t vmbus_stage;
  uint8_t stage;
  uint8_t phase;
  uint8_t offer_ready;
  uint8_t channel_ready;
  uint8_t reserved;
  int32_t last_error;
  struct vmbus_offer_info offer;
  uint32_t open_id;
  uint32_t gpadl_handle;
  uint32_t send_ring_size;
  uint32_t recv_ring_size;
  uint32_t last_gpadl_status;
  uint32_t last_open_status;
  uint32_t last_open_msgtype;
  uint32_t last_open_relid;
  uint32_t last_open_observed_id;
  uint32_t last_target_vcpu;
  uint32_t last_downstream_offset;
  uint32_t last_retry_count;
  uint32_t control_wait_budget;
  uint32_t control_expected_operation;
  struct storvsc_control_diag last_control;
  struct storvsp_channel_properties properties;
};

struct storvsc_runtime_state {
  uint8_t configured;
  uint8_t enabled;
  uint8_t phase;
  int32_t last_error;
  struct storvsc_backend_ops ops;
  struct storvsc_backend_state backend;
};

void storvsc_runtime_init(struct storvsc_runtime_state *state);
int storvsc_runtime_configure(struct storvsc_runtime_state *state,
                              int hyperv_present,
                              const struct storvsc_backend_ops *ops);
void storvsc_runtime_set_enabled(struct storvsc_runtime_state *state, int enabled);
void storvsc_runtime_degrade_passive(struct storvsc_runtime_state *state);
int storvsc_runtime_step_probe_only(struct storvsc_runtime_state *state);
int storvsc_runtime_step(struct storvsc_runtime_state *state);
int storvsc_runtime_controller_status(const struct storvsc_runtime_state *state,
                                      struct storvsc_controller_status *out);

#endif /* DRIVERS_STORAGE_STORVSC_RUNTIME_H */
