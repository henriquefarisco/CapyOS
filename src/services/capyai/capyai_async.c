#include "services/capyai_async.h"

#include "kernel/spinlock.h"
#include "kernel/worker.h"

struct capyai_async_runtime {
  struct spinlock lock;
  int initialized;
  int initializing;
  int pool_id;
  enum capyai_async_status state;
  uint64_t next_job_id;
  uint64_t current_job_id;
  int detached;
  struct capyai_async_request_v2 request;
  struct capyai_async_response response;
};

struct capyai_async_dispatch_bridge {
  const struct capyai_async_request_v2 *request;
  char detail[CAPYAI_SUMMARY_MAX];
};

static struct capyai_async_runtime g_async = {
    .lock = SPINLOCK_INIT,
    .pool_id = -1,
    .state = CAPYAI_ASYNC_IDLE,
    .next_job_id = 1u,
};

static void async_zero(void *ptr, size_t size) {
  uint8_t *p = (uint8_t *)ptr;
  size_t i;
  if (!p) return;
  for (i = 0u; i < size; ++i) p[i] = 0u;
}

static void async_copy(char *dst, size_t dst_size, const char *src) {
  size_t i = 0u;
  if (!dst || dst_size == 0u) return;
  for (; src && src[i] && i + 1u < dst_size; ++i) dst[i] = src[i];
  dst[i] = '\0';
}

static int async_dispatch_bridge(void *ctx, const char *command_line) {
  struct capyai_async_dispatch_bridge *bridge =
      (struct capyai_async_dispatch_bridge *)ctx;
  if (!bridge || !bridge->request || !bridge->request->base.dispatch) return 127;
  bridge->detail[0] = '\0';
  return bridge->request->base.dispatch(bridge->request->base.dispatch_ctx,
                                        command_line, bridge->detail,
                                        sizeof(bridge->detail));
}

static void capyai_async_worker(void *arg) {
  struct capyai_async_request_v2 request;
  struct capyai_async_response response;
  struct capyai_async_dispatch_bridge bridge;
  const char *model;
  size_t model_len = 0u;
  uint64_t job_id;
  int rc;
  uint64_t flags;
  (void)arg;

  async_zero(&request, sizeof(request));
  async_zero(&response, sizeof(response));
  async_zero(&bridge, sizeof(bridge));

  spin_lock_irqsave(&g_async.lock, &flags);
  if (g_async.state != CAPYAI_ASYNC_QUEUED) {
    spin_unlock_irqrestore(&g_async.lock, flags);
    return;
  }
  request = g_async.request;
  job_id = g_async.current_job_id;
  g_async.state = CAPYAI_ASYNC_RUNNING;
  spin_unlock_irqrestore(&g_async.lock, flags);

  response.job_id = job_id;
  response.client_generation = request.base.client_generation;
  response.status = CAPYAI_ASYNC_DONE;
  response.session = request.base.session;
  bridge.request = &request;
  model = capyai_builtin_model(&model_len);
  rc = capyai_execute_intent_v2(
      model, model_len, request.base.intent, "capy", "capysh",
      &request.base.perms,
      &response.session,
      request.base.dispatch ? async_dispatch_bridge : (capyai_dispatch_fn)0,
      request.base.dispatch ? &bridge : (void *)0,
      request.typed_dispatch, request.typed_dispatch_ctx,
      &response.plan, &response.result);
  if (bridge.detail[0]) {
    async_copy(response.result.detail, sizeof(response.result.detail),
               bridge.detail);
  }
  if (rc != 0) {
    response.status = CAPYAI_ASYNC_FAILED;
    async_copy(response.summary, sizeof(response.summary),
               "Falha interna ao processar o pedido.");
  } else {
    capyai_summary(&response.plan, &response.result, response.summary,
                   sizeof(response.summary));
  }

  spin_lock_irqsave(&g_async.lock, &flags);
  if (g_async.current_job_id == job_id) {
    if (g_async.detached) {
      g_async.state = CAPYAI_ASYNC_IDLE;
      g_async.current_job_id = 0u;
      g_async.detached = 0;
      async_zero(&g_async.request, sizeof(g_async.request));
      async_zero(&g_async.response, sizeof(g_async.response));
    } else {
      g_async.response = response;
      g_async.state = response.status;
    }
  }
  spin_unlock_irqrestore(&g_async.lock, flags);
}

int capyai_async_init(void) {
  int pool_id;
  uint64_t flags;
  spin_lock_irqsave(&g_async.lock, &flags);
  if (g_async.initialized) {
    spin_unlock_irqrestore(&g_async.lock, flags);
    return CAPYAI_ASYNC_OK;
  }
  if (g_async.initializing) {
    spin_unlock_irqrestore(&g_async.lock, flags);
    return CAPYAI_ASYNC_ERR_UNAVAILABLE;
  }
  g_async.initializing = 1;
  g_async.pool_id = -1;
  g_async.state = CAPYAI_ASYNC_IDLE;
  if (g_async.next_job_id == 0u) g_async.next_job_id = 1u;
  spin_unlock_irqrestore(&g_async.lock, flags);

  pool_id = worker_pool_create("capyai", 1u);
  if (pool_id < 0) {
    spin_lock_irqsave(&g_async.lock, &flags);
    g_async.initializing = 0;
    spin_unlock_irqrestore(&g_async.lock, flags);
    return CAPYAI_ASYNC_ERR_UNAVAILABLE;
  }

  spin_lock_irqsave(&g_async.lock, &flags);
  g_async.pool_id = pool_id;
  g_async.initialized = 1;
  g_async.initializing = 0;
  spin_unlock_irqrestore(&g_async.lock, flags);
  return CAPYAI_ASYNC_OK;
}

static int capyai_async_submit_normalized(
    const struct capyai_async_request_v2 *request, uint64_t *out_job_id) {
  uint64_t job_id;
  uint64_t flags;
  int pool_id;
  if (!request || !out_job_id || request->base.intent[0] == '\0') {
    return CAPYAI_ASYNC_ERR_INVALID;
  }
  if (capyai_async_init() != CAPYAI_ASYNC_OK) {
    return CAPYAI_ASYNC_ERR_UNAVAILABLE;
  }

  spin_lock_irqsave(&g_async.lock, &flags);
  if (g_async.state != CAPYAI_ASYNC_IDLE) {
    spin_unlock_irqrestore(&g_async.lock, flags);
    return CAPYAI_ASYNC_ERR_BUSY;
  }
  job_id = g_async.next_job_id++;
  if (job_id == 0u) job_id = g_async.next_job_id++;
  g_async.request = *request;
  g_async.request.base.intent[sizeof(g_async.request.base.intent) - 1u] = '\0';
  g_async.current_job_id = job_id;
  g_async.detached = 0;
  g_async.state = CAPYAI_ASYNC_QUEUED;
  pool_id = g_async.pool_id;
  spin_unlock_irqrestore(&g_async.lock, flags);

  if (worker_pool_submit((uint32_t)pool_id, capyai_async_worker, (void *)0) !=
      0) {
    spin_lock_irqsave(&g_async.lock, &flags);
    if (g_async.current_job_id == job_id &&
        g_async.state == CAPYAI_ASYNC_QUEUED) {
      g_async.state = CAPYAI_ASYNC_IDLE;
      g_async.current_job_id = 0u;
      async_zero(&g_async.request, sizeof(g_async.request));
    }
    spin_unlock_irqrestore(&g_async.lock, flags);
    return CAPYAI_ASYNC_ERR_UNAVAILABLE;
  }
  *out_job_id = job_id;
  return CAPYAI_ASYNC_OK;
}

int capyai_async_submit(const struct capyai_async_request *request,
                        uint64_t *out_job_id) {
  struct capyai_async_request_v2 normalized;
  if (!request) return CAPYAI_ASYNC_ERR_INVALID;
  async_zero(&normalized, sizeof(normalized));
  normalized.abi_version = CAPYAI_ASYNC_REQUEST_ABI_V2;
  normalized.struct_size = (uint32_t)sizeof(normalized);
  normalized.base = *request;
  return capyai_async_submit_normalized(&normalized, out_job_id);
}

int capyai_async_submit_v2(const struct capyai_async_request_v2 *request,
                           uint64_t *out_job_id) {
  if (!request || request->abi_version != CAPYAI_ASYNC_REQUEST_ABI_V2 ||
      request->struct_size < (uint32_t)sizeof(*request)) {
    return CAPYAI_ASYNC_ERR_INVALID;
  }
  return capyai_async_submit_normalized(request, out_job_id);
}

int capyai_async_poll(uint64_t job_id, struct capyai_async_response *out) {
  uint64_t flags;
  if (job_id == 0u || !out) return CAPYAI_ASYNC_ERR_INVALID;
  spin_lock_irqsave(&g_async.lock, &flags);
  if (g_async.current_job_id != job_id) {
    spin_unlock_irqrestore(&g_async.lock, flags);
    return CAPYAI_ASYNC_ERR_STALE;
  }
  if (g_async.state == CAPYAI_ASYNC_QUEUED ||
      g_async.state == CAPYAI_ASYNC_RUNNING) {
    spin_unlock_irqrestore(&g_async.lock, flags);
    return 0;
  }
  if (g_async.state != CAPYAI_ASYNC_DONE &&
      g_async.state != CAPYAI_ASYNC_FAILED) {
    spin_unlock_irqrestore(&g_async.lock, flags);
    return CAPYAI_ASYNC_ERR_STALE;
  }
  *out = g_async.response;
  g_async.state = CAPYAI_ASYNC_IDLE;
  g_async.current_job_id = 0u;
  g_async.detached = 0;
  async_zero(&g_async.request, sizeof(g_async.request));
  async_zero(&g_async.response, sizeof(g_async.response));
  spin_unlock_irqrestore(&g_async.lock, flags);
  return 1;
}

int capyai_async_detach(uint64_t job_id) {
  uint64_t flags;
  if (job_id == 0u) return CAPYAI_ASYNC_ERR_INVALID;
  spin_lock_irqsave(&g_async.lock, &flags);
  if (g_async.current_job_id != job_id) {
    spin_unlock_irqrestore(&g_async.lock, flags);
    return CAPYAI_ASYNC_ERR_STALE;
  }
  if (g_async.state == CAPYAI_ASYNC_DONE ||
      g_async.state == CAPYAI_ASYNC_FAILED) {
    g_async.state = CAPYAI_ASYNC_IDLE;
    g_async.current_job_id = 0u;
    g_async.detached = 0;
    async_zero(&g_async.request, sizeof(g_async.request));
    async_zero(&g_async.response, sizeof(g_async.response));
  } else {
    g_async.detached = 1;
  }
  spin_unlock_irqrestore(&g_async.lock, flags);
  return CAPYAI_ASYNC_OK;
}

int capyai_async_busy(void) {
  int busy;
  uint64_t flags;
  spin_lock_irqsave(&g_async.lock, &flags);
  busy = g_async.initialized && g_async.state != CAPYAI_ASYNC_IDLE;
  spin_unlock_irqrestore(&g_async.lock, flags);
  return busy;
}

enum capyai_async_status capyai_async_state(void) {
  enum capyai_async_status state;
  uint64_t flags;
  spin_lock_irqsave(&g_async.lock, &flags);
  state = g_async.initialized ? g_async.state : CAPYAI_ASYNC_IDLE;
  spin_unlock_irqrestore(&g_async.lock, flags);
  return state;
}

#ifdef UNIT_TEST
void capyai_async_reset_for_test(void) {
  async_zero(&g_async, sizeof(g_async));
  g_async.pool_id = -1;
  g_async.state = CAPYAI_ASYNC_IDLE;
  g_async.next_job_id = 1u;
}
#endif
