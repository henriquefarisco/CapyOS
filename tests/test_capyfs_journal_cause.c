#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs/capyfs.h"
#include "fs/capyfs_journal_integration.h"
#include "fs/journal.h"
#include "fs/block.h"

#define JCAUSE_BLOCK_SIZE 512u
#define JCAUSE_BLOCK_COUNT 64u

struct mem_backend {
    uint32_t block_size;
    uint32_t block_count;
    uint8_t *data;
};

static int mem_read(void *ctx, uint32_t blk, void *buf) {
    struct mem_backend *m = (struct mem_backend *)ctx;
    if (!m || !buf || blk >= m->block_count) return -1;
    memcpy(buf, m->data + (size_t)blk * m->block_size, m->block_size);
    return 0;
}

static int mem_write(void *ctx, uint32_t blk, const void *buf) {
    struct mem_backend *m = (struct mem_backend *)ctx;
    if (!m || !buf || blk >= m->block_count) return -1;
    memcpy(m->data + (size_t)blk * m->block_size, buf, m->block_size);
    return 0;
}

static const struct block_device_ops g_mem_ops = {
    .read_block  = mem_read,
    .write_block = mem_write,
};

static int expect_true(int cond, const char *msg) {
    if (!cond) { fprintf(stderr, "[journal-cause] %s\n", msg); return 1; }
    return 0;
}

/* data_start must be > CAPYFS_JOURNAL_BLOCKS (32) + 2 */
#define DATA_START 40u
#define JRNL_START  8u  /* DATA_START - 32 = 8 */

/* Each test uses its own backend + dev to avoid exhausting the 4-slot table. */

static int test_cause_label_none(void) {
    int fails = 0;
    fails += expect_true(
        strcmp(capyfs_journal_recovery_cause_label(CAPYFS_JOURNAL_RECOVERY_NONE), "none") == 0,
        "NONE label");
    fails += expect_true(
        strcmp(capyfs_journal_recovery_cause_label(CAPYFS_JOURNAL_RECOVERY_WAL_REPLAY),
               "wal-replay") == 0,
        "WAL_REPLAY label");
    fails += expect_true(
        strcmp(capyfs_journal_recovery_cause_label(CAPYFS_JOURNAL_RECOVERY_WAL_REPLAY_FAILED),
               "wal-replay-failed") == 0,
        "WAL_REPLAY_FAILED label");
    fails += expect_true(
        strcmp(capyfs_journal_recovery_cause_label(CAPYFS_JOURNAL_RECOVERY_FORMAT),
               "first-mount-format") == 0,
        "FORMAT label");
    return fails;
}

static int test_unformatted_causes_format(void) {
    struct mem_backend mem;
    struct block_device dev;
    int fails = 0;

    mem.block_size  = JCAUSE_BLOCK_SIZE;
    mem.block_count = JCAUSE_BLOCK_COUNT;
    mem.data = (uint8_t *)calloc(JCAUSE_BLOCK_COUNT, JCAUSE_BLOCK_SIZE);
    dev.name = "mem-fmt"; dev.block_size = JCAUSE_BLOCK_SIZE;
    dev.block_count = JCAUSE_BLOCK_COUNT; dev.ctx = &mem; dev.ops = &g_mem_ops;

    /* Do NOT pre-format — journal region is zeroed (invalid magic) */
    fails += expect_true(
        capyfs_journal_mount_hook(&dev, DATA_START, NULL, 0) == 0,
        "mount_hook on unformatted region should succeed");
    fails += expect_true(capyfs_journal_last_recovery_cause() == CAPYFS_JOURNAL_RECOVERY_FORMAT,
                         "unformatted region: cause must be FORMAT");

    free(mem.data);
    return fails;
}

static int test_clean_mount_cause_none(void) {
    struct mem_backend mem;
    struct block_device dev;
    struct journal j;
    int fails = 0;

    mem.block_size  = JCAUSE_BLOCK_SIZE;
    mem.block_count = JCAUSE_BLOCK_COUNT;
    mem.data = (uint8_t *)calloc(JCAUSE_BLOCK_COUNT, JCAUSE_BLOCK_SIZE);
    dev.name = "mem-clean"; dev.block_size = JCAUSE_BLOCK_SIZE;
    dev.block_count = JCAUSE_BLOCK_COUNT; dev.ctx = &mem; dev.ops = &g_mem_ops;

    /* Pre-format the journal region so mount_hook sees a clean journal */
    fails += expect_true(journal_format(&j, &dev, JRNL_START, 32u) == 0,
                         "pre-format should succeed");

    fails += expect_true(
        capyfs_journal_mount_hook(&dev, DATA_START, NULL, 0) == 0,
        "mount_hook on clean journal should succeed");
    fails += expect_true(capyfs_journal_last_recovery_cause() == CAPYFS_JOURNAL_RECOVERY_NONE,
                         "clean mount: cause must be NONE");

    free(mem.data);
    return fails;
}

static int test_dirty_shutdown_causes_wal_replay(void) {
    struct mem_backend mem;
    struct block_device dev;
    struct journal j;
    struct journal_transaction txn;
    uint8_t payload[JCAUSE_BLOCK_SIZE];
    int fails = 0;

    mem.block_size  = JCAUSE_BLOCK_SIZE;
    mem.block_count = JCAUSE_BLOCK_COUNT;
    mem.data = (uint8_t *)calloc(JCAUSE_BLOCK_COUNT, JCAUSE_BLOCK_SIZE);
    dev.name = "mem-dirty"; dev.block_size = JCAUSE_BLOCK_SIZE;
    dev.block_count = JCAUSE_BLOCK_COUNT; dev.ctx = &mem; dev.ops = &g_mem_ops;

    fails += expect_true(journal_format(&j, &dev, JRNL_START, 32u) == 0, "pre-format");

    memset(payload, 0xBBu, sizeof(payload));
    fails += expect_true(journal_begin(&j, &txn) == 0, "journal_begin");
    fails += expect_true(
        journal_log_block(&txn, 50u, payload, 0u, JCAUSE_BLOCK_SIZE) == 0,
        "journal_log_block");
    fails += expect_true(journal_commit(&txn) == 0, "journal_commit");

    /* mount_hook on the same device: should detect needs_replay and replay */
    fails += expect_true(
        capyfs_journal_mount_hook(&dev, DATA_START, NULL, 0) == 0,
        "mount_hook after dirty shutdown should succeed");
    fails += expect_true(
        capyfs_journal_last_recovery_cause() == CAPYFS_JOURNAL_RECOVERY_WAL_REPLAY,
        "dirty shutdown: cause must be WAL_REPLAY");

    free(mem.data);
    return fails;
}

static int test_root_secret_install_clear(void) {
    int fails = 0;
    static const uint8_t SECRET[] = {
        0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x01,0x02,
        0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,
    };

    capyfs_journal_clear_root_secret();
    fails += expect_true(
        capyfs_journal_root_secret_installed() == 0,
        "no secret installed at start");

    capyfs_journal_install_root_secret(SECRET, sizeof(SECRET));
    fails += expect_true(
        capyfs_journal_root_secret_installed() == 1,
        "install_root_secret should mark installed");

    capyfs_journal_clear_root_secret();
    fails += expect_true(
        capyfs_journal_root_secret_installed() == 0,
        "clear_root_secret should reset state");

    /* Empty/oversized secrets must NOT install. */
    capyfs_journal_install_root_secret(NULL, 0);
    fails += expect_true(
        capyfs_journal_root_secret_installed() == 0,
        "NULL secret stays uninstalled");

    return fails;
}

static int test_authenticated_mount_with_super(void) {
    struct mem_backend mem;
    struct block_device dev;
    int fails = 0;
    static const uint8_t SECRET[] = "capyos-test-root-secret-32-bytes";
    /* Two distinct superblocks must produce two distinct mounts that each
     * format their own authenticated journal. */
    struct capy_super super_a;
    struct capy_super super_b;

    mem.block_size  = JCAUSE_BLOCK_SIZE;
    mem.block_count = JCAUSE_BLOCK_COUNT;
    mem.data = (uint8_t *)calloc(JCAUSE_BLOCK_COUNT, JCAUSE_BLOCK_SIZE);
    dev.name = "mem-auth"; dev.block_size = JCAUSE_BLOCK_SIZE;
    dev.block_count = JCAUSE_BLOCK_COUNT; dev.ctx = &mem; dev.ops = &g_mem_ops;

    memset(&super_a, 0, sizeof(super_a));
    super_a.magic = CAPYFS_MAGIC; super_a.version = CAPYFS_VERSION;
    super_a.block_size = CAPYFS_BLOCK_SIZE; super_a.block_count = 1024u;
    super_a.inode_count = 64u; super_a.data_start = DATA_START;

    memset(&super_b, 0xFF, sizeof(super_b));
    super_b.magic = CAPYFS_MAGIC; super_b.version = CAPYFS_VERSION;
    super_b.block_size = CAPYFS_BLOCK_SIZE; super_b.block_count = 2048u;
    super_b.inode_count = 128u; super_b.data_start = DATA_START;

    capyfs_journal_install_root_secret(SECRET, sizeof(SECRET) - 1);

    /* Format with super_a: journal is created authenticated. */
    fails += expect_true(
        capyfs_journal_mount_hook(&dev, DATA_START,
                                  &super_a, sizeof(super_a)) == 0,
        "auth mount_hook on unformatted should succeed");
    fails += expect_true(
        capyfs_journal_last_recovery_cause() == CAPYFS_JOURNAL_RECOVERY_FORMAT,
        "auth first mount: cause must be FORMAT");

    capyfs_journal_clear_root_secret();
    free(mem.data);
    (void)super_b; /* reserved for future cross-volume HMAC tests */
    return fails;
}

int run_capyfs_journal_cause_tests(void) {
    int fails = 0;
    fails += test_cause_label_none();
    fails += test_unformatted_causes_format();
    fails += test_clean_mount_cause_none();
    fails += test_dirty_shutdown_causes_wal_replay();
    fails += test_root_secret_install_clear();
    fails += test_authenticated_mount_with_super();
    if (fails == 0) {
        printf("[tests] capyfs_journal_cause OK\n");
    }
    return fails;
}
