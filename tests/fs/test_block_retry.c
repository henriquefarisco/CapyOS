/* Host tests for the unified block_device retry loop (Slice 3E.2.B).
 *
 * The driver is mocked with a programmable sequence of classes so
 * we can audit the budget and the reset escalation deterministically.
 */

#include "drivers/storage/block_error.h"
#include "fs/block.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void fail(const char *msg) {
    printf("[block-retry] FAIL: %s\n", msg);
    g_failures++;
}

#define MOCK_SEQ_MAX 16

struct mock_ctx {
    enum block_io_error_class seq[MOCK_SEQ_MAX];
    int seq_len;
    int seq_idx;
    int read_calls;
    int write_calls;
    int reset_calls;
    int reset_result; /* 0 success, -1 failure */
};

static enum block_io_error_class mock_read_ex(void *opaque, uint32_t blk,
                                              void *buf) {
    struct mock_ctx *m = (struct mock_ctx *)opaque;
    enum block_io_error_class cls;
    (void)blk;
    (void)buf;
    if (m->seq_idx >= m->seq_len) {
        return BLOCK_IO_OK;
    }
    cls = m->seq[m->seq_idx++];
    m->read_calls++;
    return cls;
}

static enum block_io_error_class mock_write_ex(void *opaque, uint32_t blk,
                                               const void *buf) {
    struct mock_ctx *m = (struct mock_ctx *)opaque;
    enum block_io_error_class cls;
    (void)blk;
    (void)buf;
    if (m->seq_idx >= m->seq_len) {
        return BLOCK_IO_OK;
    }
    cls = m->seq[m->seq_idx++];
    m->write_calls++;
    return cls;
}

static int mock_reset(void *opaque) {
    struct mock_ctx *m = (struct mock_ctx *)opaque;
    m->reset_calls++;
    return m->reset_result;
}

static void setup_dev(struct block_device *dev, struct block_device_ops *ops,
                      struct mock_ctx *m) {
    memset(m, 0, sizeof(*m));
    m->reset_result = 0;
    memset(ops, 0, sizeof(*ops));
    ops->read_block_ex = mock_read_ex;
    ops->write_block_ex = mock_write_ex;
    ops->reset = mock_reset;
    memset(dev, 0, sizeof(*dev));
    dev->block_size = 512;
    dev->block_count = 1024;
    dev->ctx = m;
    dev->ops = ops;
}

/* ---- Tests ---- */

static void test_immediate_success(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    enum block_io_error_class cls;
    setup_dev(&dev, &ops, &m);
    m.seq[0] = BLOCK_IO_OK;
    m.seq_len = 1;
    cls = block_device_read_ex(&dev, 0, buf);
    if (cls != BLOCK_IO_OK) fail("immediate OK must return OK");
    if (m.read_calls != 1) fail("immediate OK must dispatch exactly once");
    if (m.reset_calls != 0) fail("immediate OK must not invoke reset");
}

static void test_transient_recovers_within_budget(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    enum block_io_error_class cls;
    setup_dev(&dev, &ops, &m);
    m.seq[0] = BLOCK_IO_ERR_TRANSIENT;
    m.seq[1] = BLOCK_IO_ERR_TRANSIENT;
    m.seq[2] = BLOCK_IO_OK;
    m.seq_len = 3;
    cls = block_device_read_ex(&dev, 0, buf);
    if (cls != BLOCK_IO_OK) fail("transient->OK within budget must return OK");
    if (m.read_calls != 3) fail("must dispatch 3 times for 2 retries");
    if (m.reset_calls != 0) fail("transient must not trigger reset");
}

static void test_transient_exhausts_budget(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    enum block_io_error_class cls;
    setup_dev(&dev, &ops, &m);
    /* 4 TRANSIENT in a row = initial + 3 retries = budget exhausted. */
    for (int i = 0; i < 4; i++) m.seq[i] = BLOCK_IO_ERR_TRANSIENT;
    m.seq_len = 4;
    cls = block_device_read_ex(&dev, 0, buf);
    if (cls != BLOCK_IO_ERR_TRANSIENT) {
        fail("exhausted transient budget must surface TRANSIENT");
    }
    if (m.read_calls != 4) fail("must dispatch exactly 4 times (1 + 3 retries)");
}

static void test_permanent_no_retry(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    enum block_io_error_class cls;
    setup_dev(&dev, &ops, &m);
    m.seq[0] = BLOCK_IO_ERR_PERMANENT;
    m.seq[1] = BLOCK_IO_OK; /* would mask the bug if loop continued */
    m.seq_len = 2;
    cls = block_device_read_ex(&dev, 0, buf);
    if (cls != BLOCK_IO_ERR_PERMANENT) fail("PERMANENT must surface immediately");
    if (m.read_calls != 1) fail("PERMANENT must not retry");
}

static void test_device_gone_no_retry(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    enum block_io_error_class cls;
    setup_dev(&dev, &ops, &m);
    m.seq[0] = BLOCK_IO_ERR_DEVICE_GONE;
    m.seq_len = 1;
    cls = block_device_read_ex(&dev, 0, buf);
    if (cls != BLOCK_IO_ERR_DEVICE_GONE) {
        fail("DEVICE_GONE must surface immediately");
    }
    if (m.read_calls != 1) fail("DEVICE_GONE must not retry");
    if (m.reset_calls != 0) fail("DEVICE_GONE must not invoke reset");
}

static void test_timeout_resets_and_retries(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    enum block_io_error_class cls;
    setup_dev(&dev, &ops, &m);
    m.seq[0] = BLOCK_IO_ERR_TIMEOUT;
    m.seq[1] = BLOCK_IO_OK; /* second attempt after reset succeeds */
    m.seq_len = 2;
    cls = block_device_read_ex(&dev, 0, buf);
    if (cls != BLOCK_IO_OK) fail("TIMEOUT must recover via reset + retry");
    if (m.reset_calls != 1) fail("TIMEOUT must invoke reset exactly once");
    if (m.read_calls != 2) {
        fail("TIMEOUT must dispatch exactly twice (initial + 1 retry)");
    }
}

static void test_timeout_no_reset_op_is_permanent(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    enum block_io_error_class cls;
    setup_dev(&dev, &ops, &m);
    ops.reset = NULL; /* driver has no reset path */
    m.seq[0] = BLOCK_IO_ERR_TIMEOUT;
    m.seq_len = 1;
    cls = block_device_read_ex(&dev, 0, buf);
    if (cls != BLOCK_IO_ERR_PERMANENT) {
        fail("TIMEOUT with no reset op must surface as PERMANENT");
    }
    if (m.read_calls != 1) fail("must not retry without reset");
}

static void test_timeout_reset_fails_is_permanent(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    enum block_io_error_class cls;
    setup_dev(&dev, &ops, &m);
    m.reset_result = -1;
    m.seq[0] = BLOCK_IO_ERR_TIMEOUT;
    m.seq_len = 1;
    cls = block_device_read_ex(&dev, 0, buf);
    if (cls != BLOCK_IO_ERR_PERMANENT) {
        fail("TIMEOUT with failing reset must surface as PERMANENT");
    }
    if (m.reset_calls != 1) fail("must attempt reset exactly once");
    if (m.read_calls != 1) fail("must not retry if reset failed");
}

static void test_timeout_followed_by_timeout_surfaces(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    enum block_io_error_class cls;
    setup_dev(&dev, &ops, &m);
    /* Timeout, reset succeeds, retry also times out. Budget = 1,
     * so the second TIMEOUT must surface (we do not loop forever
     * resetting). */
    m.seq[0] = BLOCK_IO_ERR_TIMEOUT;
    m.seq[1] = BLOCK_IO_ERR_TIMEOUT;
    m.seq_len = 2;
    cls = block_device_read_ex(&dev, 0, buf);
    if (cls != BLOCK_IO_ERR_TIMEOUT) {
        fail("second TIMEOUT after reset must surface as TIMEOUT");
    }
    if (m.reset_calls != 1) fail("reset must be attempted only once");
    if (m.read_calls != 2) fail("must dispatch exactly 1 initial + 1 retry");
}

static void test_write_path_applies_same_policy(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    enum block_io_error_class cls;
    setup_dev(&dev, &ops, &m);
    m.seq[0] = BLOCK_IO_ERR_TRANSIENT;
    m.seq[1] = BLOCK_IO_OK;
    m.seq_len = 2;
    cls = block_device_write_ex(&dev, 0, buf);
    if (cls != BLOCK_IO_OK) fail("write retry must recover from TRANSIENT");
    if (m.write_calls != 2) fail("write must dispatch twice (1 + 1 retry)");
    if (m.read_calls != 0) fail("write path must not invoke read_block_ex");
}

static void test_invalid_input_is_permanent(void) {
    struct block_device dev;
    struct block_device_ops ops;
    struct mock_ctx m;
    uint8_t buf[16];
    setup_dev(&dev, &ops, &m);
    if (block_device_read_ex(NULL, 0, buf) != BLOCK_IO_ERR_PERMANENT) {
        fail("NULL dev must be PERMANENT");
    }
    if (block_device_read_ex(&dev, 0, NULL) != BLOCK_IO_ERR_PERMANENT) {
        fail("NULL buffer must be PERMANENT");
    }
    if (block_device_read_ex(&dev, dev.block_count, buf) !=
        BLOCK_IO_ERR_PERMANENT) {
        fail("out-of-range block must be PERMANENT");
    }
    if (m.read_calls != 0) fail("invalid input must not dispatch read");
}

/* Legacy ABI fast-path: a driver that only exposes read_block must
 * NOT pay the retry loop overhead (zero reset calls). */
static int legacy_read(void *ctx, uint32_t blk, void *buf) {
    (void)blk;
    (void)buf;
    int *calls = (int *)ctx;
    (*calls)++;
    return 0;
}

static void test_legacy_driver_no_retry_overhead(void) {
    struct block_device dev;
    struct block_device_ops ops;
    int calls = 0;
    uint8_t buf[16];
    int rc;
    memset(&ops, 0, sizeof(ops));
    ops.read_block = legacy_read;
    memset(&dev, 0, sizeof(dev));
    dev.block_size = 512;
    dev.block_count = 10;
    dev.ctx = &calls;
    dev.ops = &ops;
    rc = block_device_read(&dev, 0, buf);
    if (rc != 0) fail("legacy driver must succeed");
    if (calls != 1) fail("legacy driver must dispatch exactly once");
}

int run_block_retry_tests(void) {
    g_failures = 0;
    test_immediate_success();
    test_transient_recovers_within_budget();
    test_transient_exhausts_budget();
    test_permanent_no_retry();
    test_device_gone_no_retry();
    test_timeout_resets_and_retries();
    test_timeout_no_reset_op_is_permanent();
    test_timeout_reset_fails_is_permanent();
    test_timeout_followed_by_timeout_surfaces();
    test_write_path_applies_same_policy();
    test_invalid_input_is_permanent();
    test_legacy_driver_no_retry_overhead();
    if (g_failures == 0) printf("[tests] block_retry OK\n");
    return g_failures;
}
