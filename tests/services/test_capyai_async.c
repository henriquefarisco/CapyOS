#include <stdio.h>
#include <string.h>

#include "kernel/worker.h"
#include "services/capyai_async.h"

static int failures;
static int pool_create_result;
static int submit_result;
static int pool_create_calls;
static int submit_calls;
static int execute_result;
static worker_fn pending_fn;
static void *pending_arg;
static char observed_intent[CAPYAI_ASYNC_INTENT_MAX];
static int observed_allow_write;
static uint32_t observed_turns;

#define CHECK(cond, label)                                                    \
  do {                                                                        \
    if (cond) printf("  ok   %s\n", label);                                  \
    else { printf("  FAIL %s\n", label); failures++; }                       \
  } while (0)

void spin_lock(struct spinlock *lock) { lock->locked = 1u; }
void spin_unlock(struct spinlock *lock) { lock->locked = 0u; }
void spin_lock_irqsave(struct spinlock *lock, uint64_t *flags) {
  *flags = 0u;
  spin_lock(lock);
}
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
  (void)flags;
  spin_unlock(lock);
}
int spin_trylock(struct spinlock *lock) {
  if (lock->locked) return -1;
  lock->locked = 1u;
  return 0;
}

int worker_pool_create(const char *name, uint32_t thread_count) {
  (void)name;
  (void)thread_count;
  pool_create_calls++;
  return pool_create_result;
}
int worker_pool_submit(uint32_t pool_id, worker_fn fn, void *arg) {
  (void)pool_id;
  submit_calls++;
  if (submit_result != 0) return submit_result;
  pending_fn = fn;
  pending_arg = arg;
  return 0;
}

const char *capyai_builtin_model(size_t *out_len) {
  static const char model[] = "test-model";
  if (out_len) *out_len = sizeof(model) - 1u;
  return model;
}

int capyai_execute_intent(const char *model_text, size_t model_len,
                          const char *intent, const char *platform,
                          const char *shell,
                          const struct capyai_perms *perms,
                          struct capyai_session *session,
                          capyai_dispatch_fn dispatch, void *dispatch_ctx,
                          struct capyai_plan *plan,
                          struct capyai_exec_result *result) {
  (void)model_text;
  (void)model_len;
  (void)platform;
  (void)shell;
  strncpy(observed_intent, intent, sizeof(observed_intent) - 1u);
  observed_intent[sizeof(observed_intent) - 1u] = '\0';
  observed_allow_write = perms ? perms->allow_write : 0;
  observed_turns = session ? session->turns : 0u;
  memset(plan, 0, sizeof(*plan));
  memset(result, 0, sizeof(*result));
  plan->decision = CAPYAI_DECISION_ALLOWED;
  strcpy(plan->out.command, "list");
  plan->out.risk = CAPY_AI_RISK_READ_ONLY;
  if (execute_result != 0) return execute_result;
  if (dispatch) {
    result->executed = 1;
    result->rc = dispatch(dispatch_ctx, "list");
  }
  if (session) session->turns++;
  return 0;
}

void capyai_summary(const struct capyai_plan *plan,
                    const struct capyai_exec_result *result,
                    char *buf, size_t buf_size) {
  (void)plan;
  (void)result;
  if (buf_size > 0u) {
    strncpy(buf, "async summary", buf_size - 1u);
    buf[buf_size - 1u] = '\0';
  }
}

static int capture_dispatch(void *ctx, const char *command_line, char *detail,
                            size_t detail_size) {
  int *calls = (int *)ctx;
  (*calls)++;
  if (strcmp(command_line, "list") != 0) return 9;
  if (detail_size > 0u) {
    strncpy(detail, "captured output", detail_size - 1u);
    detail[detail_size - 1u] = '\0';
  }
  return 0;
}

static void reset_fakes(void) {
  pool_create_result = 3;
  submit_result = 0;
  pool_create_calls = 0;
  submit_calls = 0;
  execute_result = 0;
  pending_fn = NULL;
  pending_arg = NULL;
  observed_intent[0] = '\0';
  observed_allow_write = 0;
  observed_turns = 0u;
  capyai_async_reset_for_test();
}

static void prepare_request(struct capyai_async_request *request,
                            int *dispatch_calls, uint64_t generation) {
  memset(request, 0, sizeof(*request));
  strcpy(request->intent, "original request");
  request->perms.allow_write = 1;
  strcpy(request->session.last_file, "notes.txt");
  request->session.turns = 2u;
  request->dispatch = capture_dispatch;
  request->dispatch_ctx = dispatch_calls;
  request->client_generation = generation;
}

int main(void) {
  struct capyai_async_request request;
  struct capyai_async_response response;
  uint64_t job_id = 0u;
  uint64_t detached_id = 0u;
  int dispatch_calls = 0;
  printf("[test_capyai_async]\n");

  reset_fakes();
  CHECK(capyai_async_init() == CAPYAI_ASYNC_OK && pool_create_calls == 1,
        "init creates one worker pool");
  CHECK(capyai_async_init() == CAPYAI_ASYNC_OK && pool_create_calls == 1,
        "init is idempotent");

  prepare_request(&request, &dispatch_calls, 7u);
  CHECK(capyai_async_submit(&request, &job_id) == CAPYAI_ASYNC_OK &&
            job_id != 0u && submit_calls == 1,
        "submit queues and returns a token");
  strcpy(request.intent, "mutated after submit");
  request.perms.allow_write = 0;
  request.session.turns = 99u;
  CHECK(capyai_async_poll(job_id, &response) == 0,
        "poll is nonblocking while queued");
  CHECK(capyai_async_submit(&request, &detached_id) == CAPYAI_ASYNC_ERR_BUSY,
        "second submit is rejected while busy");
  pending_fn(pending_arg);
  memset(&response, 0, sizeof(response));
  CHECK(capyai_async_poll(job_id, &response) == 1 &&
            response.client_generation == 7u &&
            response.session.turns == 3u &&
            strcmp(response.summary, "async summary") == 0 &&
            strcmp(response.result.detail, "captured output") == 0,
        "completion returns copied response and captured detail");
  CHECK(strcmp(observed_intent, "original request") == 0 &&
            observed_allow_write == 1 && observed_turns == 2u &&
            dispatch_calls == 1,
        "worker consumes an owned request snapshot");
  CHECK(capyai_async_poll(job_id, &response) == CAPYAI_ASYNC_ERR_STALE,
        "completed response is consumed exactly once");

  prepare_request(&request, &dispatch_calls, 8u);
  CHECK(capyai_async_submit(&request, &detached_id) == CAPYAI_ASYNC_OK &&
            capyai_async_detach(detached_id) == CAPYAI_ASYNC_OK,
        "active job can be detached by token");
  pending_fn(pending_arg);
  CHECK(!capyai_async_busy() &&
            capyai_async_poll(detached_id, &response) == CAPYAI_ASYNC_ERR_STALE,
        "detached completion is dropped safely");

  prepare_request(&request, &dispatch_calls, 9u);
  submit_result = -1;
  CHECK(capyai_async_submit(&request, &job_id) ==
            CAPYAI_ASYNC_ERR_UNAVAILABLE && !capyai_async_busy(),
        "backend submission failure restores idle state");

  reset_fakes();
  pool_create_result = -1;
  CHECK(capyai_async_init() == CAPYAI_ASYNC_ERR_UNAVAILABLE,
        "pool creation failure is visible");

  reset_fakes();
  prepare_request(&request, &dispatch_calls, 10u);
  execute_result = -7;
  CHECK(capyai_async_submit(&request, &job_id) == CAPYAI_ASYNC_OK,
        "failed execution can still be scheduled");
  pending_fn(pending_arg);
  CHECK(capyai_async_poll(job_id, &response) == 1 &&
            response.status == CAPYAI_ASYNC_FAILED &&
            strstr(response.summary, "Falha interna") != NULL,
        "execution failure becomes a bounded async response");

  if (failures) {
    printf("[test_capyai_async] %d failure(s)\n", failures);
    return 1;
  }
  printf("[test_capyai_async] all passed\n");
  return 0;
}
