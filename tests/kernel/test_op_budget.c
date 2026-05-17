#include <stdio.h>
#include <string.h>

#include "util/op_budget.h"

static int expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "[op_budget] %s\n", msg);
        return 1;
    }
    return 0;
}

static int test_take_within_budget(void) {
    struct op_budget b;
    int fails = 0;
    op_budget_init(&b, "scan", 5u);
    fails += expect_true(op_budget_take(&b, 1u) == 1, "take 1 admitted");
    fails += expect_true(op_budget_take(&b, 2u) == 1, "take 2 admitted");
    fails += expect_true(op_budget_remaining(&b) == 2u, "remaining 2");
    fails += expect_true(op_budget_state(&b) == OP_BUDGET_OK, "state ok");
    fails += expect_true(op_budget_is_blocked(&b) == 0, "not blocked");
    return fails;
}

static int test_take_exhaust(void) {
    struct op_budget b;
    int fails = 0;
    op_budget_init(&b, "render", 3u);
    fails += expect_true(op_budget_take(&b, 1u) == 1, "1");
    fails += expect_true(op_budget_take(&b, 5u) == 0,
                         "exceeding take must return 0");
    fails += expect_true(op_budget_state(&b) == OP_BUDGET_EXHAUSTED,
                         "state exhausted");
    fails += expect_true(strstr(op_budget_reason(&b), "exhaust") != NULL,
                         "reason contains 'exhaust'");
    fails += expect_true(op_budget_remaining(&b) == 0u, "remaining 0");
    fails += expect_true(op_budget_is_blocked(&b) == 1, "blocked");
    /* Subsequent takes also return 0. */
    fails += expect_true(op_budget_take(&b, 1u) == 0, "post-exhaust take 0");
    return fails;
}

static int test_cancel_blocks_subsequent_takes(void) {
    struct op_budget b;
    int fails = 0;
    op_budget_init(&b, "fetch", 100u);
    fails += expect_true(op_budget_take(&b, 1u) == 1, "1 ok");
    op_budget_cancel(&b, "user requested abort");
    fails += expect_true(op_budget_take(&b, 1u) == 0, "after cancel returns 0");
    fails += expect_true(op_budget_state(&b) == OP_BUDGET_CANCELLED,
                         "state cancelled");
    fails += expect_true(strstr(op_budget_reason(&b), "abort") != NULL,
                         "reason contains 'abort'");
    return fails;
}

static int test_reset_clears_state(void) {
    struct op_budget b;
    int fails = 0;
    op_budget_init(&b, "io", 2u);
    fails += expect_true(op_budget_take(&b, 1u) == 1, "1 ok");
    fails += expect_true(op_budget_take(&b, 2u) == 0, "exceed");
    op_budget_reset(&b);
    fails += expect_true(op_budget_state(&b) == OP_BUDGET_OK,
                         "state ok after reset");
    fails += expect_true(op_budget_remaining(&b) == 2u,
                         "remaining restored");
    fails += expect_true(op_budget_take(&b, 2u) == 1,
                         "take admitted after reset");
    return fails;
}

static int test_explicit_exhaust(void) {
    struct op_budget b;
    int fails = 0;
    op_budget_init(&b, "decode", 1000u);
    op_budget_exhaust(&b, "downstream backpressure");
    fails += expect_true(op_budget_state(&b) == OP_BUDGET_EXHAUSTED,
                         "explicit exhaust");
    fails += expect_true(op_budget_take(&b, 1u) == 0,
                         "take refused after exhaust");
    fails += expect_true(strstr(op_budget_reason(&b), "backpressure") != NULL,
                         "reason persisted");
    return fails;
}

static int test_zero_total_means_no_budget(void) {
    struct op_budget b;
    int fails = 0;
    /* total = 0 means "no upper bound" — never exhausts on its own. */
    op_budget_init(&b, "open", 0u);
    fails += expect_true(op_budget_take(&b, 1u) == 1, "1 ok");
    fails += expect_true(op_budget_take(&b, 1000u) == 1, "1000 ok");
    fails += expect_true(op_budget_state(&b) == OP_BUDGET_OK, "ok");
    return fails;
}

int run_op_budget_tests(void) {
    int fails = 0;
    fails += test_take_within_budget();
    fails += test_take_exhaust();
    fails += test_cancel_blocks_subsequent_takes();
    fails += test_reset_clears_state();
    fails += test_explicit_exhaust();
    fails += test_zero_total_means_no_budget();
    if (fails == 0) {
        printf("[tests] op_budget OK\n");
    }
    return fails;
}
