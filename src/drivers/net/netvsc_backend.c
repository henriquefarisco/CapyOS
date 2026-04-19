#include "arch/x86_64/framebuffer_console.h"
#include "drivers/net/netvsc_backend.h"

#ifndef UNIT_TEST
#endif

#include "kernel/log/klog.h"

static void netvsc_log(const char *s) {
#ifndef UNIT_TEST
  fbcon_print(s);
#else
  (void)s;
#endif
}

static void netvsc_log_hex(uint64_t value) {
#ifndef UNIT_TEST
  fbcon_print_hex(value);
#else
  (void)value;
#endif
}

#define NETVSC_PAGE_BYTES 4096u

static uint32_t g_netvsc_next_open_id = NETVSC_BACKEND_OPEN_ID;
static uint32_t g_netvsc_next_gpadl_handle = NETVSC_BACKEND_GPADL_HANDLE;

static uint32_t netvsc_backend_alloc_open_id(void) {
  uint32_t value = g_netvsc_next_open_id++;
  if (g_netvsc_next_open_id == 0u) {
    g_netvsc_next_open_id = NETVSC_BACKEND_OPEN_ID;
  }
  return value ? value : NETVSC_BACKEND_OPEN_ID;
}

static uint32_t netvsc_backend_alloc_gpadl_handle(void) {
  uint32_t value = g_netvsc_next_gpadl_handle++;
  if (g_netvsc_next_gpadl_handle == 0u) {
    g_netvsc_next_gpadl_handle = NETVSC_BACKEND_GPADL_HANDLE;
  }
  return value ? value : NETVSC_BACKEND_GPADL_HANDLE;
}

static void netvsc_backend_memzero(void *ptr, size_t len) {
  uint8_t *p = (uint8_t *)ptr;
  if (!p) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    p[i] = 0;
  }
}

static void netvsc_backend_prepare_channel(struct netvsc_backend_state *state) {
  if (!state) {
    return;
  }
  netvsc_backend_memzero(&state->channel, sizeof(state->channel));
  state->channel.child_relid = state->offer.child_relid;
  state->channel.connection_id = state->offer.connection_id;
  state->channel.monitor_id = state->offer.monitor_id;
  state->channel.monitor_allocated = state->offer.monitor_allocated;
  state->channel.is_dedicated_interrupt = state->offer.is_dedicated_interrupt;
  state->channel.open_id = netvsc_backend_alloc_open_id();
  state->channel.gpadl_handle = netvsc_backend_alloc_gpadl_handle();
  state->channel.send_ring_size = NETVSC_BACKEND_RING_BYTES;
  state->channel.recv_ring_size = NETVSC_BACKEND_RING_BYTES;
}

void netvsc_backend_init(struct netvsc_backend_state *state) {
  if (!state) {
    return;
  }
  netvsc_backend_memzero(state, sizeof(*state));
  state->phase = NETVSC_BACKEND_PROBE;
  netvsc_session_init(&state->session, NETVSC_BACKEND_VSP_MAJOR,
                      NETVSC_BACKEND_VSP_MINOR);
}

int netvsc_backend_step(struct netvsc_backend_state *state,
                        const struct netvsc_backend_ops *ops) {
  uint8_t buffer[256];
  size_t len = 0u;
  struct netvsc_control_transport transport;

  if (!state || !ops || !ops->query_offer || !ops->open_channel ||
      !ops->send_control || !ops->recv_control) {
    return -1;
  }
  if (state->phase == NETVSC_BACKEND_READY) {
    return 0;
  }
  if (state->phase == NETVSC_BACKEND_FAILED) {
    return state->last_error ? state->last_error : -1;
  }

  if (state->phase == NETVSC_BACKEND_PROBE) {
    if (ops->query_offer(&state->offer) != 0) {
      netvsc_log("[netvsc] offer query falhou.\n");
      klog(KLOG_ERROR, "[netvsc] VMBus offer query FAILED.");
      state->phase = NETVSC_BACKEND_FAILED;
      state->last_error = -2;
      return state->last_error;
    }
    netvsc_log("[netvsc] offer relid=");
    netvsc_log_hex((uint64_t)state->offer.child_relid);
    netvsc_log(" conn=");
    netvsc_log_hex((uint64_t)state->offer.connection_id);
    netvsc_log(" mon=");
    netvsc_log_hex((uint64_t)state->offer.monitor_id);
    netvsc_log(" alloc=");
    netvsc_log_hex((uint64_t)state->offer.monitor_allocated);
    netvsc_log("\n");
    state->offer_ready = 1u;
    netvsc_backend_prepare_channel(state);
    state->phase = NETVSC_BACKEND_CHANNEL;
    klog(KLOG_INFO, "[netvsc] Offer received; advancing to CHANNEL phase.");
    klog_hex(KLOG_INFO, "[netvsc] offer relid=", (uint64_t)state->offer.child_relid);
    return 1;
  }

  if (state->phase == NETVSC_BACKEND_CHANNEL) {
    if (state->channel.last_open_msgtype == CHANNELMSG_OPENCHANNEL_RESULT &&
        state->channel.last_open_status == 0u) {
      netvsc_log(
          "[netvsc] late OPENCHANNEL ack aceito a partir do estado persistido.\n");
      state->channel_ready = 1u;
      state->open_timeout_budget = 0u;
      state->open_wait_logged = 0u;
      state->phase = NETVSC_BACKEND_CONTROL;
      return 1;
    }
    netvsc_log("[netvsc] open channel relid=");
    netvsc_log_hex((uint64_t)state->channel.child_relid);
    netvsc_log(" conn=");
    netvsc_log_hex((uint64_t)state->channel.connection_id);
    netvsc_log(" sring=");
    netvsc_log_hex((uint64_t)state->channel.send_ring_size);
    netvsc_log(" rring=");
    netvsc_log_hex((uint64_t)state->channel.recv_ring_size);
    netvsc_log(" pages=");
    netvsc_log_hex((uint64_t)((state->channel.send_ring_size +
                               state->channel.recv_ring_size) /
                              NETVSC_PAGE_BYTES));
    netvsc_log("\n");
    int rc = ops->open_channel(&state->channel);
    if (rc != 0) {
      if (state->channel.last_open_msgtype == CHANNELMSG_OPENCHANNEL_RESULT &&
          state->channel.last_open_status == 0u) {
        netvsc_log(
            "[netvsc] OPENCHANNEL retornou timeout, mas o ack de sucesso apareceu no estado do canal; promovendo mesmo assim.\n");
        state->channel_ready = 1u;
        state->open_timeout_budget = 0u;
        state->open_wait_logged = 0u;
        state->phase = NETVSC_BACKEND_CONTROL;
        return 1;
      }
      if (state->channel.last_open_status == 0xFFFFFFFCu &&
          state->open_timeout_budget < 6u) {
        state->open_timeout_budget += 1u;
        if (!state->open_wait_logged) {
          netvsc_log(
              "[netvsc] open channel ainda sem ack definitivo; mantendo o canal em observacao antes de declarar falha.\n");
          klog(KLOG_WARN, "[netvsc] OPENCHANNEL waiting for late ack (budget active).");
          state->open_wait_logged = 1u;
        }
        return 0;
      }
      netvsc_log("[netvsc] open channel falhou.\n");
      netvsc_log("[netvsc] open channel rc=");
      netvsc_log_hex((uint64_t)(uint32_t)(-rc));
      netvsc_log("\n");
      state->phase = NETVSC_BACKEND_FAILED;
      state->last_error = -3;
      return state->last_error;
    }
    netvsc_log("[netvsc] canal aberto.\n");
    state->channel.initialized = 1u;
    state->channel.opened = 1u;
    state->channel_ready = 1u;
    state->open_timeout_budget = 0u;
    state->open_wait_logged = 0u;
    state->phase = NETVSC_BACKEND_CONTROL;
    return 1;
  }

  if (state->phase != NETVSC_BACKEND_CONTROL) {
    return 0;
  }

  if (state->waiting_response) {
    int rc = ops->recv_control(&state->channel, buffer, sizeof(buffer), &len);
    if (rc < 0) {
      netvsc_log("[netvsc] recv control falhou.\n");
      state->phase = NETVSC_BACKEND_FAILED;
      state->last_error = -4;
      return state->last_error;
    }
    if (rc == 0 || len == 0u) {
      return 0;
    }
    rc = netvsc_session_handle_response(&state->session, buffer, len);
    if (rc < 0) {
      netvsc_log("[netvsc] resposta de controle invalida.\n");
      state->phase = NETVSC_BACKEND_FAILED;
      state->last_error = -5;
      return state->last_error;
    }
    state->waiting_response = 0u;
    if (netvsc_session_is_ready(&state->session)) {
      netvsc_log("[netvsc] backend pronto.\n");
      state->phase = NETVSC_BACKEND_READY;
      return 1;
    }
    return 1;
  }

  len = netvsc_session_build_next(&state->session, &transport, buffer,
                                  sizeof(buffer));
  if (len == 0u) {
    if (netvsc_session_is_ready(&state->session)) {
      state->phase = NETVSC_BACKEND_READY;
      return 1;
    }
    return 0;
  }
  if (ops->send_control(&state->channel, buffer, len) != 0) {
    netvsc_log("[netvsc] send control falhou.\n");
    state->phase = NETVSC_BACKEND_FAILED;
    state->last_error = -6;
    return state->last_error;
  }
  netvsc_log("[netvsc] controle enviado.\n");
  state->waiting_response = 1u;
  return 1;
}

int netvsc_backend_is_ready(const struct netvsc_backend_state *state) {
  return state && state->phase == NETVSC_BACKEND_READY;
}
