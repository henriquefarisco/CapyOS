#include "drivers/storage/storvsc_backend.h"

#ifndef UNIT_TEST
extern void fbcon_print(const char *s);
extern void fbcon_print_hex(uint64_t val);
#endif

static void storvsc_log(const char *s) {
#ifndef UNIT_TEST
  fbcon_print(s);
#else
  (void)s;
#endif
}

static void storvsc_log_hex(uint64_t value) {
#ifndef UNIT_TEST
  fbcon_print_hex(value);
#else
  (void)value;
#endif
}

#define STORVSC_PAGE_BYTES 4096u
#define STORVSC_CONTROL_WAIT_LIMIT 24u

static uint32_t g_storvsc_next_open_id = STORVSC_BACKEND_OPEN_ID;
static uint32_t g_storvsc_next_gpadl_handle = STORVSC_BACKEND_GPADL_HANDLE;

static uint32_t storvsc_backend_alloc_open_id(void) {
  uint32_t value = g_storvsc_next_open_id++;
  if (g_storvsc_next_open_id == 0u) {
    g_storvsc_next_open_id = STORVSC_BACKEND_OPEN_ID;
  }
  return value ? value : STORVSC_BACKEND_OPEN_ID;
}

static uint32_t storvsc_backend_alloc_gpadl_handle(void) {
  uint32_t value = g_storvsc_next_gpadl_handle++;
  if (g_storvsc_next_gpadl_handle == 0u) {
    g_storvsc_next_gpadl_handle = STORVSC_BACKEND_GPADL_HANDLE;
  }
  return value ? value : STORVSC_BACKEND_GPADL_HANDLE;
}

static void storvsc_backend_zero(void *dst, size_t len) {
  uint8_t *bytes = (uint8_t *)dst;
  if (!bytes) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    bytes[i] = 0u;
  }
}

static const char *storvsc_session_phase_name(uint8_t phase) {
  switch (phase) {
  case STORVSC_SESSION_WAIT_BEGIN:
    return "wait-begin";
  case STORVSC_SESSION_WAIT_VERSION:
    return "wait-version";
  case STORVSC_SESSION_WAIT_PROPERTIES:
    return "wait-properties";
  case STORVSC_SESSION_WAIT_END:
    return "wait-end";
  case STORVSC_SESSION_WAIT_ENUMERATE:
    return "wait-enumerate";
  case STORVSC_SESSION_READY:
    return "ready";
  case STORVSC_SESSION_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

static void storvsc_backend_reset_control_wait(
    struct storvsc_backend_state *state) {
  if (!state) {
    return;
  }
  state->control_wait_budget = 0u;
  state->control_wait_logged = 0u;
}

static int storvsc_backend_note_control_wait(struct storvsc_backend_state *state,
                                             const char *reason) {
  if (!state) {
    return 0;
  }
  if (state->control_wait_budget < 0xFFu) {
    state->control_wait_budget += 1u;
  }
  if (!state->control_wait_logged) {
    storvsc_log("[storvsc] controle ainda inconclusivo: ");
    storvsc_log(reason ? reason : "sem detalhe");
    storvsc_log(" phase=");
    storvsc_log(storvsc_session_phase_name(state->session.phase));
    storvsc_log(" waits=");
    storvsc_log_hex((uint64_t)state->control_wait_budget);
    storvsc_log(" recv=");
    storvsc_log_hex((uint64_t)(uint32_t)state->last_control.read_rc);
    storvsc_log(" extract=");
    storvsc_log_hex((uint64_t)(uint32_t)state->last_control.extract_rc);
    storvsc_log(" parse=");
    storvsc_log_hex((uint64_t)(uint32_t)state->last_control.parse_rc);
    storvsc_log(" op=");
    storvsc_log_hex((uint64_t)state->last_control.operation);
    storvsc_log(" expected=");
    storvsc_log_hex((uint64_t)state->session.last_request_operation);
    storvsc_log(" status=");
    storvsc_log_hex((uint64_t)state->last_control.status);
    storvsc_log(" trans=");
    storvsc_log_hex(state->last_control.trans_id);
    storvsc_log(" expected_trans=");
    storvsc_log_hex((uint64_t)STORVSC_CONTROL_TRANS_ID);
    storvsc_log("\n");
    state->control_wait_logged = 1u;
  }
  return state->control_wait_budget >= STORVSC_CONTROL_WAIT_LIMIT;
}

static void storvsc_backend_prepare_channel(struct storvsc_backend_state *state) {
  if (!state) {
    return;
  }
  storvsc_backend_zero(&state->channel, sizeof(state->channel));
  state->channel.child_relid = state->offer.child_relid;
  state->channel.connection_id = state->offer.connection_id;
  state->channel.monitor_id = state->offer.monitor_id;
  state->channel.monitor_allocated = state->offer.monitor_allocated;
  state->channel.is_dedicated_interrupt = state->offer.is_dedicated_interrupt;
  state->channel.open_id = storvsc_backend_alloc_open_id();
  state->channel.gpadl_handle = storvsc_backend_alloc_gpadl_handle();
  state->channel.send_ring_size = STORVSC_BACKEND_RING_BYTES;
  state->channel.recv_ring_size = STORVSC_BACKEND_RING_BYTES;
}

void storvsc_backend_init(struct storvsc_backend_state *state) {
  if (!state) {
    return;
  }
  storvsc_backend_zero(state, sizeof(*state));
  state->phase = STORVSC_BACKEND_PROBE;
  storvsc_session_init(&state->session, STORVSP_PROTO_WIN10);
}

int storvsc_backend_step(struct storvsc_backend_state *state,
                         const struct storvsc_backend_ops *ops) {
  uint8_t buffer[STORVSP_WIRE_PACKET_SIZE];
  size_t len = 0u;

  if (!state || !ops || !ops->query_offer || !ops->open_channel ||
      !ops->send_control || !ops->recv_control) {
    return -1;
  }
  if (state->phase == STORVSC_BACKEND_READY) {
    return 0;
  }
  if (state->phase == STORVSC_BACKEND_FAILED) {
    return state->last_error ? state->last_error : -1;
  }

  if (state->phase == STORVSC_BACKEND_PROBE) {
    if (ops->query_offer(&state->offer) != 0) {
      storvsc_log("[storvsc] offer query falhou.\n");
      state->phase = STORVSC_BACKEND_FAILED;
      state->last_error = -2;
      return state->last_error;
    }
    storvsc_log("[storvsc] offer relid=");
    storvsc_log_hex((uint64_t)state->offer.child_relid);
    storvsc_log(" conn=");
    storvsc_log_hex((uint64_t)state->offer.connection_id);
    storvsc_log(" mon=");
    storvsc_log_hex((uint64_t)state->offer.monitor_id);
    storvsc_log(" alloc=");
    storvsc_log_hex((uint64_t)state->offer.monitor_allocated);
    storvsc_log("\n");
    state->offer_ready = 1u;
    storvsc_backend_prepare_channel(state);
    state->phase = STORVSC_BACKEND_CHANNEL;
    return 1;
  }

  if (state->phase == STORVSC_BACKEND_CHANNEL) {
    if (state->channel.last_open_msgtype == CHANNELMSG_OPENCHANNEL_RESULT &&
        state->channel.last_open_status == 0u) {
      storvsc_log(
          "[storvsc] late OPENCHANNEL ack aceito a partir do estado persistido.\n");
      state->channel_ready = 1u;
      state->open_timeout_budget = 0u;
      state->open_wait_logged = 0u;
      storvsc_backend_reset_control_wait(state);
      state->phase = STORVSC_BACKEND_CONTROL;
      return 1;
    }
    storvsc_log("[storvsc] open channel relid=");
    storvsc_log_hex((uint64_t)state->channel.child_relid);
    storvsc_log(" conn=");
    storvsc_log_hex((uint64_t)state->channel.connection_id);
    storvsc_log(" sring=");
    storvsc_log_hex((uint64_t)state->channel.send_ring_size);
    storvsc_log(" rring=");
    storvsc_log_hex((uint64_t)state->channel.recv_ring_size);
    storvsc_log(" pages=");
    storvsc_log_hex((uint64_t)((state->channel.send_ring_size +
                                state->channel.recv_ring_size) /
                               STORVSC_PAGE_BYTES));
    storvsc_log("\n");
    int rc = ops->open_channel(&state->channel);
    if (rc != 0) {
      if (state->channel.last_open_msgtype == CHANNELMSG_OPENCHANNEL_RESULT &&
          state->channel.last_open_status == 0u) {
        storvsc_log(
            "[storvsc] OPENCHANNEL retornou timeout, mas o ack de sucesso apareceu no estado do canal; promovendo mesmo assim.\n");
        state->channel_ready = 1u;
        state->open_timeout_budget = 0u;
        state->open_wait_logged = 0u;
        storvsc_backend_reset_control_wait(state);
        state->phase = STORVSC_BACKEND_CONTROL;
        return 1;
      }
      if (state->channel.last_open_status == 0xFFFFFFFCu &&
          state->open_timeout_budget < 3u) {
        state->open_timeout_budget += 1u;
        if (!state->open_wait_logged) {
          storvsc_log(
              "[storvsc] open channel ainda sem ack definitivo; mantendo o canal em observacao antes de declarar falha.\n");
          state->open_wait_logged = 1u;
        }
        return 0;
      }
      storvsc_log("[storvsc] open channel falhou.\n");
      storvsc_log("[storvsc] open channel rc=");
      storvsc_log_hex((uint64_t)(uint32_t)(-rc));
      storvsc_log("\n");
      state->phase = STORVSC_BACKEND_FAILED;
      state->last_error = -3;
      return state->last_error;
    }
    storvsc_log("[storvsc] canal aberto.\n");
    state->channel.initialized = 1u;
    state->channel.opened = 1u;
    state->channel_ready = 1u;
    state->open_timeout_budget = 0u;
    state->open_wait_logged = 0u;
    storvsc_backend_reset_control_wait(state);
    state->phase = STORVSC_BACKEND_CONTROL;
    return 1;
  }

  if (state->phase != STORVSC_BACKEND_CONTROL) {
    return 0;
  }

  if (state->waiting_response) {
    struct storvsc_session_state next_session;
    int rc = 0;

    storvsc_backend_zero(&state->last_control, sizeof(state->last_control));
    rc = ops->recv_control(&state->channel, buffer, sizeof(buffer), &len,
                           &state->last_control);
    state->last_control.read_rc = rc;
    if (rc < 0) {
      if (!storvsc_backend_note_control_wait(state,
                                             "recv_control retornou erro")) {
        return 0;
      }
      storvsc_log("[storvsc] recv control falhou apos janela de observacao.\n");
      state->phase = STORVSC_BACKEND_FAILED;
      state->last_error = -4;
      return state->last_error;
    }
    if (rc == 0 || len == 0u) {
      if (!storvsc_backend_note_control_wait(
              state, "nenhum payload de controle disponivel ainda")) {
        return 0;
      }
      storvsc_log("[storvsc] controle nao respondeu dentro da janela local.\n");
      state->phase = STORVSC_BACKEND_FAILED;
      state->last_error = -4;
      return state->last_error;
    }

    state->last_control.packet_len = (uint32_t)len;
    if (state->last_control.trans_id != 0u &&
        state->last_control.trans_id != (uint64_t)STORVSC_CONTROL_TRANS_ID) {
      if (!storvsc_backend_note_control_wait(
              state, "mensagem de outro trans_id ignorada")) {
        return 0;
      }
      storvsc_log(
          "[storvsc] controle permaneceu sem resposta apos trans_id divergente.\n");
      state->phase = STORVSC_BACKEND_FAILED;
      state->last_error = -4;
      return state->last_error;
    }
    next_session = state->session;
    rc = storvsc_session_handle_response(&next_session, buffer, len);
    state->last_control.parse_rc = rc;
    if (rc == 0) {
      if (!storvsc_backend_note_control_wait(
              state, "mensagem fora da fase de controle em espera")) {
        return 0;
      }
      storvsc_log(
          "[storvsc] controle nao confirmou a fase esperada dentro da janela local.\n");
      state->phase = STORVSC_BACKEND_FAILED;
      state->last_error = -5;
      return state->last_error;
    }
    if (rc < 0) {
      if (state->last_control.status == STORVSP_STATUS_SUCCESS &&
          !storvsc_backend_note_control_wait(
              state, "pacote de controle nao casou com a fase esperada")) {
        return 0;
      }
      storvsc_log("[storvsc] resposta de controle invalida.\n");
      storvsc_log("[storvsc] resposta invalida phase=");
      storvsc_log(storvsc_session_phase_name(state->session.phase));
      storvsc_log(" op=");
      storvsc_log_hex((uint64_t)state->last_control.operation);
      storvsc_log(" status=");
      storvsc_log_hex((uint64_t)state->last_control.status);
      storvsc_log(" parse_rc=");
      storvsc_log_hex((uint64_t)(uint32_t)(-rc));
      storvsc_log("\n");
      state->phase = STORVSC_BACKEND_FAILED;
      state->last_error = -5;
      return state->last_error;
    }
    state->session = next_session;
    storvsc_backend_reset_control_wait(state);
    state->waiting_response = 0u;
    if (storvsc_session_is_ready(&state->session)) {
      storvsc_log("[storvsc] backend pronto.\n");
      state->phase = STORVSC_BACKEND_READY;
      return 1;
    }
    return 1;
  }

  len = storvsc_session_build_next(&state->session, buffer, sizeof(buffer));
  if (len == 0u) {
    if (storvsc_session_is_ready(&state->session)) {
      state->phase = STORVSC_BACKEND_READY;
      return 1;
    }
    return 0;
  }
  if (ops->send_control(&state->channel, buffer, len) != 0) {
    storvsc_log("[storvsc] send control falhou.\n");
    state->phase = STORVSC_BACKEND_FAILED;
    state->last_error = -6;
    return state->last_error;
  }
  storvsc_log("[storvsc] controle enviado phase=");
  storvsc_log(storvsc_session_phase_name(state->session.phase));
  storvsc_log(" len=");
  storvsc_log_hex((uint64_t)len);
  storvsc_log("\n");
  storvsc_backend_reset_control_wait(state);
  state->waiting_response = 1u;
  return 1;
}

int storvsc_backend_is_ready(const struct storvsc_backend_state *state) {
  return state && state->phase == STORVSC_BACKEND_READY;
}
