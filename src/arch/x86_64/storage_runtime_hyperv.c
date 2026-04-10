#include "storage_runtime_hyperv.h"

#include "arch/x86_64/storage_runtime_hyperv_plan.h"
#include "core/version.h"
#include "drivers/hyperv/hyperv.h"
#include "drivers/storage/storvsc_vmbus.h"

extern void fbcon_print(const char *s);
extern void fbcon_print_hex(uint64_t val);

static const char *bool_label(int value) { return value ? "yes" : "no"; }

static const char *storvsc_phase_label(uint8_t phase) {
  switch (phase) {
  case STORVSC_RUNTIME_DISABLED:
    return "disabled";
  case STORVSC_RUNTIME_PROBE:
    return "probe";
  case STORVSC_RUNTIME_CHANNEL:
    return "channel";
  case STORVSC_RUNTIME_CONTROL:
    return "control";
  case STORVSC_RUNTIME_READY:
    return "ready";
  case STORVSC_RUNTIME_FAILED:
    return "failed";
  default:
    return "unconfigured";
  }
}

static void build_runtime_plan(
    const struct x64_storage_hyperv_runtime_state *state,
    int boot_services_active, int uses_firmware, int allow_hybrid_prepare,
    struct x64_storage_hyperv_plan *plan) {
  struct storvsc_controller_status status;

  if (!plan) {
    return;
  }

  plan->gate_state = X64_STORAGE_HYPERV_GATE_INVALID;
  plan->next_action = X64_STORAGE_HYPERV_ACTION_INVALID;
  if (!state) {
    return;
  }
  if (storvsc_runtime_controller_status(&state->runtime, &status) != 0) {
    status.configured = 0u;
    status.enabled = 0u;
    status.phase = STORVSC_RUNTIME_UNCONFIGURED;
  }

  x64_storage_hyperv_plan_build(
      state->present ? 1u : 0u, status.configured, status.enabled, status.phase,
      x64_storage_hyperv_runtime_bus_prepared(state) ? 1u : 0u,
      x64_storage_hyperv_runtime_bus_connected(state) ? 1u : 0u,
      x64_storage_hyperv_runtime_offer_cached(state) ? 1u : 0u,
      allow_hybrid_prepare ? 1u : 0u,
      boot_services_active, uses_firmware, plan);
}

static void log_runtime_plan(const struct x64_storage_hyperv_runtime_state *state,
                             const struct x64_storage_hyperv_plan *plan,
                             int boot_services_active, int uses_firmware) {
  struct storvsc_controller_status status;
  int prepared = 0;
  int connected = 0;
  int cached_offer = 0;
  const char *block = "n/a";
  static uint8_t last_gate = 0xFFu;
  static uint8_t last_next = 0xFFu;
  static uint8_t last_phase = 0xFFu;
  static int last_boot_services = -1;
  static int last_uses_firmware = -1;
  static int last_allow_hybrid = -1;
  static int last_prepared = -1;
  static int last_connected = -1;
  static int last_offer = -1;
  static int last_configured = -1;
  static int last_enabled = -1;
  static int32_t last_error = 0x7FFFFFFF;
  static uint32_t last_relid = 0xFFFFFFFFu;
  static uint32_t last_conn = 0xFFFFFFFFu;

  if (!state || !plan) {
    return;
  }
  if (storvsc_runtime_controller_status(
          (struct storvsc_runtime_state *)&state->runtime, &status) != 0) {
    status.configured = 0u;
    status.enabled = 0u;
    status.phase = STORVSC_RUNTIME_UNCONFIGURED;
    status.offer_ready = 0u;
    status.last_error = 0;
    status.offer.child_relid = 0u;
    status.offer.connection_id = 0u;
  }

  prepared = x64_storage_hyperv_runtime_bus_prepared(state) ? 1 : 0;
  connected = x64_storage_hyperv_runtime_bus_connected(state) ? 1 : 0;
  cached_offer = x64_storage_hyperv_runtime_offer_cached(state) ? 1 : 0;
  block = x64_storage_hyperv_runtime_block_reason(
      state, boot_services_active, uses_firmware,
      state->hybrid_prepare_allowed ? 1 : 0);

  if (plan->gate_state == last_gate && plan->next_action == last_next &&
      status.phase == last_phase &&
      boot_services_active == last_boot_services &&
      uses_firmware == last_uses_firmware &&
      state->hybrid_prepare_allowed == last_allow_hybrid &&
      prepared == last_prepared && connected == last_connected &&
      cached_offer == last_offer && status.configured == last_configured &&
      status.enabled == last_enabled && status.last_error == last_error &&
      status.offer.child_relid == last_relid &&
      status.offer.connection_id == last_conn) {
    return;
  }

  fbcon_print("[storvsc-plan] gate=");
  fbcon_print(x64_storage_hyperv_gate_label(plan->gate_state));
  fbcon_print(" next=");
  fbcon_print(x64_storage_hyperv_action_label(plan->next_action));
  fbcon_print(" block=");
  fbcon_print(block);
  fbcon_print(" bootsvc=");
  fbcon_print(bool_label(boot_services_active));
  fbcon_print(" firmware=");
  fbcon_print(bool_label(uses_firmware));
  fbcon_print(" allow=");
  fbcon_print(bool_label(state->hybrid_prepare_allowed));
  fbcon_print(" prepared=");
  fbcon_print(bool_label(prepared));
  fbcon_print(" connected=");
  fbcon_print(bool_label(connected));
  fbcon_print(" offer=");
  fbcon_print(bool_label(cached_offer));
  fbcon_print(" cfg=");
  fbcon_print(bool_label(status.configured));
  fbcon_print(" enabled=");
  fbcon_print(bool_label(status.enabled));
  fbcon_print(" cooloff=0x");
  fbcon_print_hex((uint64_t)state->cooldown_remaining);
  fbcon_print(" phase=");
  fbcon_print(storvsc_phase_label(status.phase));
  if (status.offer_ready) {
    fbcon_print(" relid=0x");
    fbcon_print_hex((uint64_t)status.offer.child_relid);
    fbcon_print(" conn=0x");
    fbcon_print_hex((uint64_t)status.offer.connection_id);
  }
  if (status.last_error != 0) {
    fbcon_print(" last_error=0x");
    fbcon_print_hex((uint64_t)(uint32_t)status.last_error);
  }
  fbcon_print("\n");

  last_gate = plan->gate_state;
  last_next = plan->next_action;
  last_phase = status.phase;
  last_boot_services = boot_services_active;
  last_uses_firmware = uses_firmware;
  last_allow_hybrid = state->hybrid_prepare_allowed;
  last_prepared = prepared;
  last_connected = connected;
  last_offer = cached_offer;
  last_configured = status.configured;
  last_enabled = status.enabled;
  last_error = status.last_error;
  last_relid = status.offer.child_relid;
  last_conn = status.offer.connection_id;
}

static void storvsc_runtime_note_success(
    struct x64_storage_hyperv_runtime_state *state) {
  if (!state) {
    return;
  }
  state->cooldown_remaining = 0u;
  state->failure_streak = 0u;
  state->last_failure_code = 0;
  state->cooldown_logged = 0;
}

static void storvsc_runtime_note_failure(
    struct x64_storage_hyperv_runtime_state *state, int rc) {
  if (!state || rc >= 0) {
    return;
  }
  if (state->last_failure_code == rc) {
    state->failure_streak += 1u;
  } else {
    state->failure_streak = 1u;
    state->last_failure_code = rc;
  }
  state->cooldown_remaining = state->failure_streak < 2u
                                  ? 2u
                                  : (state->failure_streak >= 8u
                                         ? 24u
                                         : state->failure_streak * 4u);
  state->cooldown_logged = 0;
}

static int finish_runtime_action(
    struct x64_storage_hyperv_runtime_state *state, uint8_t action,
    int result) {
  struct storvsc_controller_status status;
  uint8_t stage = HYPERV_VMBUS_STAGE_OFF;
  static uint8_t last_stage = 0xFFu;
  static uint8_t last_action = 0xFFu;
  static int32_t last_result = 0x7FFFFFFF;
  static uint32_t last_relid = 0xFFFFFFFFu;
  static uint32_t last_conn = 0xFFFFFFFFu;

  if (!state) {
    return result;
  }
  state->last_action = action;
  state->last_result = result;
  state->action_attempts += 1u;
  if (result > 0) {
    state->action_changes += 1u;
  }
  if (x64_storage_hyperv_runtime_controller_status(state, &status) == 0) {
    stage = status.stage;
    if (stage != last_stage || action != last_action || result != last_result ||
        status.offer.child_relid != last_relid ||
        status.offer.connection_id != last_conn) {
      fbcon_print("[storvsc] build=");
      fbcon_print(CAPYOS_VERSION_FULL);
      fbcon_print(" feature=");
      fbcon_print(CAPYOS_FEATURE_HYPERV_RUNTIME);
      fbcon_print(" stage=");
      fbcon_print(hyperv_vmbus_stage_label(stage));
      fbcon_print(" action=0x");
      fbcon_print_hex((uint64_t)action);
      fbcon_print(" result=0x");
      fbcon_print_hex((uint64_t)(uint32_t)result);
      fbcon_print(" relid=0x");
      fbcon_print_hex((uint64_t)status.offer.child_relid);
      fbcon_print(" conn=0x");
      fbcon_print_hex((uint64_t)status.offer.connection_id);
      fbcon_print("\n");
      last_stage = stage;
      last_action = action;
      last_result = result;
      last_relid = status.offer.child_relid;
      last_conn = status.offer.connection_id;
    }
  }
  return result;
}

void x64_storage_hyperv_runtime_reset(
    struct x64_storage_hyperv_runtime_state *state) {
  struct storvsc_backend_ops ops;

  if (!state) {
    return;
  }

  storvsc_runtime_init(&state->runtime);
  state->present = hyperv_detect() ? 1 : 0;
  state->hybrid_prepare_allowed = 0;
  state->wait_bus_logged = 0;
  state->wait_offer_logged = 0;
  state->wait_runtime_logged = 0;
  state->enabled_logged = 0;
  state->probe_logged = 0;
  state->prepare_logged = 0;
  state->ready_logged = 0;
  state->fallback_logged = 0;
  state->cooldown_logged = 0;
  state->last_action = X64_STORAGE_HYPERV_ACTION_INVALID;
  state->last_result = 0;
  state->last_failure_code = 0;
  state->action_attempts = 0u;
  state->action_changes = 0u;
  state->cooldown_remaining = 0u;
  state->failure_streak = 0u;
  if (!state->present) {
    return;
  }
  ops = storvsc_vmbus_ops();
  if (storvsc_runtime_configure(&state->runtime, 1, &ops) == 0) {
    storvsc_runtime_set_enabled(&state->runtime, 0);
  }
}

int x64_storage_hyperv_runtime_present(
    const struct x64_storage_hyperv_runtime_state *state) {
  return state ? state->present : 0;
}

int x64_storage_hyperv_runtime_bus_prepared(
    const struct x64_storage_hyperv_runtime_state *state) {
  return state && state->present ? vmbus_runtime_prepared() : 0;
}

int x64_storage_hyperv_runtime_bus_connected(
    const struct x64_storage_hyperv_runtime_state *state) {
  return state && state->present ? storvsc_vmbus_bus_connected() : 0;
}

static int x64_storage_hyperv_runtime_query_offer(
    const struct x64_storage_hyperv_runtime_state *state) {
  struct vmbus_offer_info offer;

  if (!state || !state->present) {
    return -1;
  }
  if (storvsc_vmbus_bus_connected()) {
    return storvsc_vmbus_offer_refresh_connected(&offer);
  }
  return storvsc_vmbus_offer_cached(&offer);
}

int x64_storage_hyperv_runtime_offer_cached(
    const struct x64_storage_hyperv_runtime_state *state) {
  return x64_storage_hyperv_runtime_query_offer(state) == 0;
}

const char *x64_storage_hyperv_runtime_phase_name(
    const struct x64_storage_hyperv_runtime_state *state) {
  struct storvsc_controller_status status;

  if (!state ||
      storvsc_runtime_controller_status((struct storvsc_runtime_state *)&state->runtime,
                                        &status) != 0) {
    return "unconfigured";
  }
  switch (status.phase) {
  case STORVSC_RUNTIME_DISABLED:
    return "disabled";
  case STORVSC_RUNTIME_PROBE:
    return "probe";
  case STORVSC_RUNTIME_CHANNEL:
    return "channel";
  case STORVSC_RUNTIME_CONTROL:
    return "control";
  case STORVSC_RUNTIME_READY:
    return "ready";
  case STORVSC_RUNTIME_FAILED:
    return "failed";
  default:
    return "unconfigured";
  }
}

uint8_t x64_storage_hyperv_runtime_gate_state(
    const struct x64_storage_hyperv_runtime_state *state,
    int boot_services_active, int uses_firmware, int allow_hybrid_prepare) {
  struct x64_storage_hyperv_plan plan;

  build_runtime_plan(state, boot_services_active, uses_firmware,
                     allow_hybrid_prepare, &plan);
  return plan.gate_state;
}

uint8_t x64_storage_hyperv_runtime_next_action(
    const struct x64_storage_hyperv_runtime_state *state,
    int boot_services_active, int uses_firmware, int allow_hybrid_prepare) {
  struct x64_storage_hyperv_plan plan;

  build_runtime_plan(state, boot_services_active, uses_firmware,
                     allow_hybrid_prepare, &plan);
  return plan.next_action;
}

const char *x64_storage_hyperv_runtime_block_reason(
    const struct x64_storage_hyperv_runtime_state *state,
    int boot_services_active, int uses_firmware, int allow_hybrid_prepare) {
  struct x64_storage_hyperv_plan plan;
  struct storvsc_controller_status status;

  build_runtime_plan(state, boot_services_active, uses_firmware,
                     allow_hybrid_prepare, &plan);
  if (!state || !state->present || plan.gate_state == X64_STORAGE_HYPERV_GATE_INVALID) {
    return "n/a";
  }

  switch (plan.gate_state) {
  case X64_STORAGE_HYPERV_GATE_WAIT_PLATFORM:
    return uses_firmware ? "platform-hybrid" : "boot-services-active";
  case X64_STORAGE_HYPERV_GATE_PREPARE_BUS:
    return "vmbus-unprepared";
  case X64_STORAGE_HYPERV_GATE_WAIT_BUS:
    return "vmbus-disconnected";
  case X64_STORAGE_HYPERV_GATE_WAIT_OFFER:
    return "offer-miss";
  case X64_STORAGE_HYPERV_GATE_WAIT_RUNTIME:
    return "policy-disabled";
  default:
    break;
  }

  if (storvsc_runtime_controller_status((struct storvsc_runtime_state *)&state->runtime,
                                        &status) != 0) {
    return "unconfigured";
  }
  if (!status.enabled) {
    return "policy-disabled";
  }
  if (status.phase == STORVSC_RUNTIME_READY) {
    return "none";
  }
  if (status.phase == STORVSC_RUNTIME_FAILED) {
    return "failed";
  }
  return "in-progress";
}

void x64_storage_hyperv_runtime_allow_hybrid_prepare(
    struct x64_storage_hyperv_runtime_state *state, int allow) {
  if (!state) {
    return;
  }
  state->hybrid_prepare_allowed = allow ? 1 : 0;
}

int x64_storage_hyperv_runtime_controller_status(
    const struct x64_storage_hyperv_runtime_state *state,
    struct storvsc_controller_status *out) {
  uint8_t vmbus_stage = 0u;

  if (!state || !state->present) {
    return -1;
  }
  if (storvsc_runtime_controller_status(
          (struct storvsc_runtime_state *)&state->runtime, out) != 0) {
    return -1;
  }
  vmbus_stage = vmbus_runtime_stage();
  if (out->offer_ready && vmbus_stage < HYPERV_VMBUS_STAGE_OFFERS) {
    vmbus_stage = HYPERV_VMBUS_STAGE_OFFERS;
  }
  out->vmbus_stage = vmbus_stage;
  out->stage = hyperv_runtime_stage_for(
      vmbus_stage, out->configured, out->offer_ready, out->channel_ready,
      out->phase, out->last_error);
  return 0;
}

int x64_storage_hyperv_runtime_try_prepare_bus(
    struct x64_storage_hyperv_runtime_state *state,
    void (*print)(const char *)) {
  int rc = 0;

  if (!state || !state->present || vmbus_runtime_prepared()) {
    return finish_runtime_action(state, X64_STORAGE_HYPERV_ACTION_PREPARE_BUS,
                                 0);
  }
  rc = vmbus_runtime_prepare();
  if (rc == 0 && !state->prepare_logged && print) {
    print("[storage] Hyper-V VMBus base preparada; negociacao ainda desativada.\n");
    state->prepare_logged = 1;
  }
  return finish_runtime_action(state, X64_STORAGE_HYPERV_ACTION_PREPARE_BUS,
                               rc == 0 ? 1 : rc);
}

int x64_storage_hyperv_runtime_try_enable_native(
    struct x64_storage_hyperv_runtime_state *state, int boot_services_active,
    int uses_firmware, int allow_hybrid_prepare, void (*print)(const char *)) {
  struct x64_storage_hyperv_plan plan;
  struct storvsc_controller_status status;
  int rc = 0;

  if (!state || !state->present) {
    return 0;
  }
  if (storvsc_runtime_controller_status(&state->runtime, &status) != 0) {
    return -1;
  }
  if (status.ready) {
    return 0;
  }
  state->hybrid_prepare_allowed = allow_hybrid_prepare ? 1 : 0;
  build_runtime_plan(state, boot_services_active, uses_firmware,
                     state->hybrid_prepare_allowed, &plan);
  log_runtime_plan(state, &plan, boot_services_active, uses_firmware);

  if (state->cooldown_remaining > 0u &&
      (plan.next_action == X64_STORAGE_HYPERV_ACTION_ENABLE_PROBE ||
       plan.next_action == X64_STORAGE_HYPERV_ACTION_WAIT_OFFER ||
       plan.next_action == X64_STORAGE_HYPERV_ACTION_STEP_PROBE ||
       plan.next_action == X64_STORAGE_HYPERV_ACTION_STEP_RUNTIME)) {
    state->cooldown_remaining -= 1u;
    if (!state->cooldown_logged && print) {
      print("[storage] Hyper-V StorVSC em cooloff temporario; evitando repetir o mesmo timeout de canal a cada janela curta.\n");
      state->cooldown_logged = 1;
    }
    return finish_runtime_action(state, plan.next_action, 0);
  }

  switch (plan.next_action) {
  case X64_STORAGE_HYPERV_ACTION_PREPARE_BUS:
    return x64_storage_hyperv_runtime_try_prepare_bus(state, print);
  case X64_STORAGE_HYPERV_ACTION_WAIT_BUS:
    rc = vmbus_runtime_connect();
    if (rc == 0) {
      if (print) {
        print("[storage] Hyper-V StorVSC conectou o VMBus em runtime nativo.\n");
      }
      state->wait_bus_logged = 0;
      state->wait_offer_logged = 0;
      return finish_runtime_action(state, plan.next_action, 1);
    }
    if (!state->wait_bus_logged && print) {
      print("[storage] Hyper-V StorVSC aguardando conexao segura do VMBus.\n");
      state->wait_bus_logged = 1;
    }
    return finish_runtime_action(state, plan.next_action, rc);
  case X64_STORAGE_HYPERV_ACTION_WAIT_OFFER:
    rc = x64_storage_hyperv_runtime_query_offer(state);
    if (rc == 0) {
      if (print) {
        print("[storage] Hyper-V StorVSC cacheou a offer apos conectar o VMBus.\n");
      }
      state->wait_offer_logged = 0;
      return finish_runtime_action(state, plan.next_action, 1);
    }
    if (rc < 0) {
      storvsc_runtime_note_failure(state, rc);
      if (!state->wait_offer_logged && print) {
        print("[storage] Hyper-V StorVSC falhou ao consultar a offer VMBus; aplicando backoff antes de nova tentativa.\n");
        state->wait_offer_logged = 1;
      }
      return finish_runtime_action(state, plan.next_action, rc);
    }
    if (!state->wait_offer_logged && print) {
      print("[storage] Hyper-V StorVSC aguardando cache da offer VMBus.\n");
      state->wait_offer_logged = 1;
    }
    return finish_runtime_action(state, plan.next_action, 0);
  case X64_STORAGE_HYPERV_ACTION_WAIT_PLATFORM:
  case X64_STORAGE_HYPERV_ACTION_WAIT_RUNTIME:
    if (plan.next_action == X64_STORAGE_HYPERV_ACTION_WAIT_RUNTIME &&
        !state->wait_runtime_logged && print) {
      print("[storage] Hyper-V StorVSC configurado, mas ainda desarmado pelo planner; aguardando budget seguro para habilitar probe/control sem quebrar o boot.\n");
      state->wait_runtime_logged = 1;
    }
    return finish_runtime_action(state, plan.next_action, 0);
  case X64_STORAGE_HYPERV_ACTION_ENABLE_PROBE:
    storvsc_runtime_set_enabled(&state->runtime, 1);
    state->wait_bus_logged = 0;
    state->wait_offer_logged = 0;
    state->wait_runtime_logged = 0;
    if (!state->enabled_logged && print) {
      print(
          "[storage] Hyper-V StorVSC armado em runtime nativo; aguardando proximo corte de controle.\n");
      state->enabled_logged = 1;
    }
    rc = storvsc_runtime_step_probe_only(&state->runtime);
    if (rc < 0) {
      storvsc_runtime_degrade_passive(&state->runtime);
      storvsc_runtime_note_failure(state, rc);
      if (!state->fallback_logged && print) {
        print("[storage] Hyper-V StorVSC falhou durante o bring-up inicial; degradando para modo passivo e mantendo o backend atual.\n");
        state->fallback_logged = 1;
      }
    } else if (rc > 0) {
      storvsc_runtime_note_success(state);
    }
    if (rc > 0 && !state->probe_logged && print) {
      print(
          "[storage] Hyper-V StorVSC validou a offer cacheada e preparou o canal offline.\n");
      state->probe_logged = 1;
    }
    return finish_runtime_action(state, plan.next_action, rc >= 0 ? 1 : rc);
  case X64_STORAGE_HYPERV_ACTION_STEP_PROBE:
    rc = storvsc_runtime_step_probe_only(&state->runtime);
    if (rc < 0) {
      storvsc_runtime_degrade_passive(&state->runtime);
      storvsc_runtime_note_failure(state, rc);
      if (!state->fallback_logged && print) {
        print("[storage] Hyper-V StorVSC falhou ao preparar o canal; degradando para modo passivo e preservando o fallback atual.\n");
        state->fallback_logged = 1;
      }
    } else if (rc > 0) {
      storvsc_runtime_note_success(state);
    }
    if (rc > 0 && !state->probe_logged && print) {
      print(
          "[storage] Hyper-V StorVSC validou a offer cacheada e preparou o canal offline.\n");
      state->probe_logged = 1;
    }
    return finish_runtime_action(state, plan.next_action, rc);
  case X64_STORAGE_HYPERV_ACTION_STEP_RUNTIME:
    rc = storvsc_runtime_step(&state->runtime);
    if (rc < 0) {
      storvsc_runtime_degrade_passive(&state->runtime);
      storvsc_runtime_note_failure(state, rc);
      if (!state->fallback_logged && print) {
        print("[storage] Hyper-V StorVSC falhou no handshake de controle; degradando para modo passivo e mantendo o backend atual.\n");
        state->fallback_logged = 1;
      }
    } else if (rc > 0) {
      storvsc_runtime_note_success(state);
    }
    if (rc > 0) {
      struct storvsc_controller_status current_status;
      if (!state->ready_logged &&
          storvsc_runtime_controller_status(&state->runtime,
                                            &current_status) == 0 &&
          current_status.ready && print) {
        print("[storage] Hyper-V StorVSC concluiu handshake de controle e ficou pronto.\n");
        state->ready_logged = 1;
      }
    }
    return finish_runtime_action(state, plan.next_action, rc);
  case X64_STORAGE_HYPERV_ACTION_NOOP:
  default:
    return finish_runtime_action(state, plan.next_action, 0);
  }
}

int x64_storage_hyperv_runtime_manual_step(
    struct x64_storage_hyperv_runtime_state *state, int boot_services_active,
    int uses_firmware, void (*print)(const char *)) {
  struct x64_storage_hyperv_plan plan;

  if (!state || !state->present) {
    return 0;
  }

  build_runtime_plan(state, boot_services_active, uses_firmware, 1, &plan);
  log_runtime_plan(state, &plan, boot_services_active, uses_firmware);
  switch (plan.gate_state) {
  case X64_STORAGE_HYPERV_GATE_PREPARE_BUS:
    return x64_storage_hyperv_runtime_try_prepare_bus(state, print);
  case X64_STORAGE_HYPERV_GATE_WAIT_PLATFORM:
    if (!state->wait_bus_logged && print) {
      print("[storage] Hyper-V StorVSC aguardando runtime nativo; conexao do VMBus segue bloqueada neste modo.\n");
      state->wait_bus_logged = 1;
    }
    return finish_runtime_action(state, X64_STORAGE_HYPERV_ACTION_WAIT_PLATFORM,
                                 0);
  case X64_STORAGE_HYPERV_GATE_WAIT_OFFER:
    if (!state->wait_offer_logged && print) {
      print("[storage] Hyper-V StorVSC aguardando runtime nativo para cachear a offer VMBus.\n");
      state->wait_offer_logged = 1;
    }
    return finish_runtime_action(state, X64_STORAGE_HYPERV_ACTION_WAIT_OFFER,
                                 0);
  default:
    return x64_storage_hyperv_runtime_try_enable_native(
        state, boot_services_active, uses_firmware, 1, print);
  }
}

uint32_t x64_storage_hyperv_runtime_attempt_count(
    const struct x64_storage_hyperv_runtime_state *state) {
  return state ? state->action_attempts : 0u;
}

uint32_t x64_storage_hyperv_runtime_change_count(
    const struct x64_storage_hyperv_runtime_state *state) {
  return state ? state->action_changes : 0u;
}

int32_t x64_storage_hyperv_runtime_last_result(
    const struct x64_storage_hyperv_runtime_state *state) {
  return state ? state->last_result : 0;
}

uint8_t x64_storage_hyperv_runtime_last_action(
    const struct x64_storage_hyperv_runtime_state *state) {
  return state ? state->last_action : X64_STORAGE_HYPERV_ACTION_INVALID;
}
