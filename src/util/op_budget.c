#include "util/op_budget.h"

#include <stddef.h>

static void op_budget_copy_reason(struct op_budget *b, const char *reason) {
    if (!b) return;
    if (!reason) {
        b->reason[0] = '\0';
        return;
    }
    size_t i = 0;
    while (reason[i] && i + 1 < OP_BUDGET_REASON_MAX) {
        b->reason[i] = reason[i];
        i++;
    }
    b->reason[i] = '\0';
}

void op_budget_init(struct op_budget *b, const char *label, uint32_t total) {
    if (!b) return;
    b->label = label;
    b->total = total;
    b->consumed = 0;
    b->state = OP_BUDGET_OK;
    b->cancel_requested = 0;
    b->reason[0] = '\0';
}

void op_budget_reset(struct op_budget *b) {
    if (!b) return;
    b->consumed = 0;
    b->state = OP_BUDGET_OK;
    b->cancel_requested = 0;
    b->reason[0] = '\0';
}

int op_budget_take(struct op_budget *b, uint32_t cost) {
    if (!b) return 0;
    if (b->cancel_requested) {
        b->state = OP_BUDGET_CANCELLED;
        if (b->reason[0] == '\0') {
            op_budget_copy_reason(b, "operation cancelled");
        }
        return 0;
    }
    if (b->state != OP_BUDGET_OK) {
        return 0;
    }
    if (cost == 0) cost = 1;
    /* Saturate consumed: protect against overflow. */
    if (b->consumed + cost < b->consumed) {
        b->consumed = b->total;
        b->state = OP_BUDGET_EXHAUSTED;
        op_budget_copy_reason(b, "budget overflow");
        return 0;
    }
    if (b->total > 0 && b->consumed + cost > b->total) {
        b->consumed = b->total;
        b->state = OP_BUDGET_EXHAUSTED;
        if (b->reason[0] == '\0') {
            op_budget_copy_reason(b, "budget exhausted");
        }
        return 0;
    }
    b->consumed += cost;
    return 1;
}

void op_budget_cancel(struct op_budget *b, const char *reason) {
    if (!b) return;
    b->cancel_requested = 1;
    b->state = OP_BUDGET_CANCELLED;
    op_budget_copy_reason(b, reason ? reason : "cancelled");
}

void op_budget_exhaust(struct op_budget *b, const char *reason) {
    if (!b) return;
    b->state = OP_BUDGET_EXHAUSTED;
    op_budget_copy_reason(b, reason ? reason : "exhausted");
    b->consumed = b->total;
}

uint32_t op_budget_remaining(const struct op_budget *b) {
    if (!b || b->total == 0) return 0;
    if (b->consumed >= b->total) return 0;
    return b->total - b->consumed;
}

uint8_t op_budget_state(const struct op_budget *b) {
    if (!b) return OP_BUDGET_OK;
    return b->state;
}

const char *op_budget_reason(const struct op_budget *b) {
    if (!b) return "";
    return b->reason;
}

int op_budget_is_blocked(const struct op_budget *b) {
    if (!b) return 0;
    return (b->state != OP_BUDGET_OK) ? 1 : 0;
}
