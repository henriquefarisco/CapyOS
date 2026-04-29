#ifndef UTIL_OP_BUDGET_H
#define UTIL_OP_BUDGET_H

#include <stdint.h>
#include <stddef.h>

/* op_budget: a small, reusable cooperative-cancellation primitive.
 *
 * Long-running operations (browser fetch/render, storage scans, network
 * loops, resource decoding) take an op_budget and call op_budget_take()
 * inside their inner loops to:
 *   - decrement remaining work units (e.g. nodes, blocks, bytes)
 *   - check for explicit cancellation
 *   - record a stable reason string when budget is exhausted
 *
 * All fields are plain integers; this is intentional so the budget can be
 * passed across modules without exposing module-private types. There is no
 * implicit allocation: callers own the struct.
 *
 * Thread-safety: not designed for concurrent producers; expected to be
 * touched from a single cooperative thread per operation.
 */

#define OP_BUDGET_REASON_MAX 64

enum op_budget_state {
    OP_BUDGET_OK = 0,
    OP_BUDGET_EXHAUSTED = 1,
    OP_BUDGET_CANCELLED = 2,
};

struct op_budget {
    const char *label;
    uint32_t total;
    uint32_t consumed;
    uint8_t state;
    uint8_t cancel_requested;
    char reason[OP_BUDGET_REASON_MAX];
};

void op_budget_init(struct op_budget *b, const char *label, uint32_t total);
void op_budget_reset(struct op_budget *b);

/* Decrement budget by `cost` (>= 1). Returns 1 if the work was admitted,
 * 0 if budget is exhausted or cancellation was already requested. When 0
 * is returned, op_budget_state(b) reports the reason. */
int op_budget_take(struct op_budget *b, uint32_t cost);

/* Mark the budget as cancelled by an external actor. Subsequent take()
 * calls return 0 with state == OP_BUDGET_CANCELLED. The reason is captured
 * for diagnostics. */
void op_budget_cancel(struct op_budget *b, const char *reason);

/* Mark exhaustion explicitly with a custom reason (used when the cost is
 * irregular or computed externally). */
void op_budget_exhaust(struct op_budget *b, const char *reason);

uint32_t op_budget_remaining(const struct op_budget *b);
uint8_t op_budget_state(const struct op_budget *b);
const char *op_budget_reason(const struct op_budget *b);
int op_budget_is_blocked(const struct op_budget *b);

#endif /* UTIL_OP_BUDGET_H */
