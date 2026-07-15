#ifndef SERVICES_CAPYAI_ASYNC_H
#define SERVICES_CAPYAI_ASYNC_H

#include <stddef.h>
#include <stdint.h>

#include "services/capyai.h"

#define CAPYAI_ASYNC_INTENT_MAX 256u

enum capyai_async_status {
  CAPYAI_ASYNC_IDLE = 0,
  CAPYAI_ASYNC_QUEUED,
  CAPYAI_ASYNC_RUNNING,
  CAPYAI_ASYNC_DONE,
  CAPYAI_ASYNC_FAILED
};

enum capyai_async_result {
  CAPYAI_ASYNC_OK = 0,
  CAPYAI_ASYNC_ERR_INVALID = -1,
  CAPYAI_ASYNC_ERR_BUSY = -2,
  CAPYAI_ASYNC_ERR_UNAVAILABLE = -3,
  CAPYAI_ASYNC_ERR_STALE = -4
};

/* Extended dispatch used by the asynchronous adapter.  The bounded detail
 * buffer captures command output without letting a worker touch a terminal or
 * compositor surface. */
typedef int (*capyai_async_dispatch_fn)(void *ctx, const char *command_line,
                                        char *detail, size_t detail_size);

/* ABI v1: keep this layout frozen.  Older modules allocate exactly this
 * structure, so typed fields must never be inserted into it. */
struct capyai_async_request {
  char intent[CAPYAI_ASYNC_INTENT_MAX];
  struct capyai_perms perms;
  struct capyai_session session;
  capyai_async_dispatch_fn dispatch;
  void *dispatch_ctx;
  uint64_t client_generation;
};

#define CAPYAI_ASYNC_REQUEST_ABI_V2 2u

/* Additive typed request.  struct_size permits future tail extension while
 * submit_v2 rejects truncated or mismatched callers before copying data. */
struct capyai_async_request_v2 {
  uint32_t abi_version;
  uint32_t struct_size;
  struct capyai_async_request base;
  capyai_typed_dispatch_fn typed_dispatch;
  void *typed_dispatch_ctx;
};

struct capyai_async_response {
  uint64_t job_id;
  uint64_t client_generation;
  enum capyai_async_status status;
  struct capyai_plan plan;
  struct capyai_exec_result result;
  struct capyai_session session;
  char summary[CAPYAI_SUMMARY_MAX];
};

/* One bounded job slot backed by one persistent kernel worker.  Requests and
 * responses are copied; no caller-owned UI pointer is dereferenced by the
 * worker. */
int capyai_async_init(void);
int capyai_async_submit(const struct capyai_async_request *request,
                        uint64_t *out_job_id);
int capyai_async_submit_v2(const struct capyai_async_request_v2 *request,
                           uint64_t *out_job_id);
/* Returns 1 and consumes a completed response, 0 while pending, or a negative
 * enum capyai_async_result on invalid/stale input. */
int capyai_async_poll(uint64_t job_id, struct capyai_async_response *out);
/* Detach a client from a queued/running job.  The operation is not killed
 * mid-side-effect; its eventual response is discarded safely. */
int capyai_async_detach(uint64_t job_id);
int capyai_async_busy(void);
enum capyai_async_status capyai_async_state(void);

#ifdef UNIT_TEST
void capyai_async_reset_for_test(void);
#endif

#endif /* SERVICES_CAPYAI_ASYNC_H */
