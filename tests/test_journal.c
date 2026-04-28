#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/journal.h"
#include "fs/block.h"

#define JOURNAL_TEST_BLOCK_SIZE 512u
#define JOURNAL_TEST_BLOCKS     64u

struct mem_backend {
  uint32_t block_size;
  uint32_t block_count;
  uint8_t *data;
};

static int mem_read(void *ctx, uint32_t block_no, void *buffer) {
  struct mem_backend *m = (struct mem_backend *)ctx;
  if (!m || !buffer || block_no >= m->block_count) return -1;
  memcpy(buffer, m->data + (size_t)block_no * m->block_size, m->block_size);
  return 0;
}

static int mem_write(void *ctx, uint32_t block_no, const void *buffer) {
  struct mem_backend *m = (struct mem_backend *)ctx;
  if (!m || !buffer || block_no >= m->block_count) return -1;
  memcpy(m->data + (size_t)block_no * m->block_size, buffer, m->block_size);
  return 0;
}

static const struct block_device_ops g_mem_ops = {
  .read_block  = mem_read,
  .write_block = mem_write,
};

static struct mem_backend g_mem;
static struct block_device g_dev;

static void setup(void) {
  g_mem.block_size  = JOURNAL_TEST_BLOCK_SIZE;
  g_mem.block_count = JOURNAL_TEST_BLOCKS;
  g_mem.data = (uint8_t *)calloc(JOURNAL_TEST_BLOCKS, JOURNAL_TEST_BLOCK_SIZE);
  g_dev.name        = "mem-journal";
  g_dev.block_size  = JOURNAL_TEST_BLOCK_SIZE;
  g_dev.block_count = JOURNAL_TEST_BLOCKS;
  g_dev.ctx         = &g_mem;
  g_dev.ops         = &g_mem_ops;
}

static void teardown(void) {
  free(g_mem.data);
  g_mem.data = NULL;
}

static int expect_true(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "[journal] %s\n", msg);
    return 1;
  }
  return 0;
}

static int test_format_and_clean_state(void) {
  struct journal j;
  int fails = 0;

  setup();
  fails += expect_true(journal_format(&j, &g_dev, 0u, 32u) == 0,
                       "journal_format should succeed on a clean device");
  fails += expect_true(journal_needs_replay(&j) == 0,
                       "freshly formatted journal should not need replay");
  teardown();
  return fails;
}

static int test_init_after_format(void) {
  struct journal j1, j2;
  int fails = 0;

  setup();
  fails += expect_true(journal_format(&j1, &g_dev, 0u, 32u) == 0,
                       "journal_format should succeed");
  fails += expect_true(journal_init(&j2, &g_dev, 0u, 32u) == 0,
                       "journal_init should succeed after format");
  fails += expect_true(journal_needs_replay(&j2) == 0,
                       "re-opened clean journal should not need replay");
  teardown();
  return fails;
}

static int test_commit_and_replay(void) {
  struct journal j;
  struct journal_transaction txn;
  uint8_t payload[JOURNAL_TEST_BLOCK_SIZE];
  uint8_t readback[JOURNAL_TEST_BLOCK_SIZE];
  int fails = 0;

  setup();

  /* target block is block 40 (outside journal region 0..31) */
  uint32_t target = 40u;

  fails += expect_true(journal_format(&j, &g_dev, 0u, 32u) == 0,
                       "journal_format should succeed");

  memset(payload, 0xABu, sizeof(payload));

  fails += expect_true(journal_begin(&j, &txn) == 0,
                       "journal_begin should succeed");
  fails += expect_true(journal_log_block(&txn, (uint64_t)target, payload,
                                         0u, JOURNAL_TEST_BLOCK_SIZE) == 0,
                       "journal_log_block should accept one block");
  fails += expect_true(journal_commit(&txn) == 0,
                       "journal_commit should succeed");

  fails += expect_true(journal_needs_replay(&j) != 0,
                       "journal should indicate pending replay after commit");

  /* Simulate a re-open (crash recovery): init from disk, then replay */
  struct journal j2;
  fails += expect_true(journal_init(&j2, &g_dev, 0u, 32u) == 0,
                       "journal_init should succeed for replay simulation");
  fails += expect_true(journal_needs_replay(&j2) != 0,
                       "re-opened journal should still need replay");

  fails += expect_true(journal_replay(&j2) == 0,
                       "journal_replay should succeed");
  fails += expect_true(journal_needs_replay(&j2) == 0,
                       "journal should be clean after replay");

  /* Verify the payload was written to the target block during replay */
  memset(readback, 0, sizeof(readback));
  fails += expect_true(block_device_read(&g_dev, target, readback) == 0,
                       "target block should be readable after replay");
  fails += expect_true(memcmp(readback, payload, JOURNAL_TEST_BLOCK_SIZE) == 0,
                       "target block should contain the replayed payload");

  teardown();
  return fails;
}

static int test_abort_does_not_replay(void) {
  struct journal j;
  struct journal_transaction txn;
  uint8_t payload[JOURNAL_TEST_BLOCK_SIZE];
  uint8_t readback[JOURNAL_TEST_BLOCK_SIZE];
  int fails = 0;

  setup();

  uint32_t target = 40u;
  memset(payload, 0xCDu, sizeof(payload));

  fails += expect_true(journal_format(&j, &g_dev, 0u, 32u) == 0,
                       "journal_format should succeed");
  fails += expect_true(journal_begin(&j, &txn) == 0,
                       "journal_begin should succeed");
  fails += expect_true(journal_log_block(&txn, (uint64_t)target, payload,
                                         0u, JOURNAL_TEST_BLOCK_SIZE) == 0,
                       "journal_log_block should succeed");
  fails += expect_true(journal_abort(&txn) == 0,
                       "journal_abort should succeed");
  fails += expect_true(journal_needs_replay(&j) == 0,
                       "aborted transaction should leave journal clean");

  /* Target block should remain zeroed */
  memset(readback, 0xFFu, sizeof(readback));
  fails += expect_true(block_device_read(&g_dev, target, readback) == 0,
                       "target block should be readable");
  uint8_t zero[JOURNAL_TEST_BLOCK_SIZE];
  memset(zero, 0, sizeof(zero));
  fails += expect_true(memcmp(readback, zero, JOURNAL_TEST_BLOCK_SIZE) == 0,
                       "target block must stay unchanged after aborted transaction");

  teardown();
  return fails;
}

static int test_checkpoint_clears_replay(void) {
  struct journal j;
  struct journal_transaction txn;
  uint8_t payload[JOURNAL_TEST_BLOCK_SIZE];
  int fails = 0;

  setup();
  memset(payload, 0x55u, sizeof(payload));

  fails += expect_true(journal_format(&j, &g_dev, 0u, 32u) == 0,
                       "journal_format should succeed");
  fails += expect_true(journal_begin(&j, &txn) == 0,
                       "journal_begin should succeed");
  fails += expect_true(journal_log_block(&txn, 40u, payload,
                                         0u, JOURNAL_TEST_BLOCK_SIZE) == 0,
                       "journal_log_block should succeed");
  fails += expect_true(journal_commit(&txn) == 0,
                       "journal_commit should succeed");
  fails += expect_true(journal_needs_replay(&j) != 0,
                       "journal should need replay after commit");

  fails += expect_true(journal_checkpoint(&j) == 0,
                       "journal_checkpoint should succeed");

  struct journal j2;
  fails += expect_true(journal_init(&j2, &g_dev, 0u, 32u) == 0,
                       "journal_init after checkpoint should succeed");
  fails += expect_true(journal_needs_replay(&j2) == 0,
                       "journal should be clean after checkpoint");

  teardown();
  return fails;
}

int run_journal_tests(void) {
  int fails = 0;
  fails += test_format_and_clean_state();
  fails += test_init_after_format();
  fails += test_commit_and_replay();
  fails += test_abort_does_not_replay();
  fails += test_checkpoint_clears_replay();
  if (fails == 0) {
    printf("[tests] journal OK\n");
  }
  return fails;
}
