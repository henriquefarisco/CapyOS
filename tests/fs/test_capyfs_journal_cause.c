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

/* M7.2 — synthetic dirty-shutdown round-trip.
 *
 * Validates that after a simulated power loss, replay restores ONLY the
 * committed transactions to the on-disk image, leaves uncommitted work
 * out, and re-mounting after replay reports a clean journal again.
 *
 * Layout of the in-memory device:
 *   - blocks 0..7    : header padding (unused for this test)
 *   - blocks 8..39   : journal region (32 blocks, JRNL_START)
 *   - blocks 40..63  : data area (DATA_START..)
 *
 * Steps:
 *   1. format the journal
 *   2. write a "pristine" pattern to data block 50, 51 directly via the
 *      backend so we know the baseline
 *   3. begin TX1, log a payload for block 50, COMMIT
 *   4. begin TX2, log a payload for block 51, COMMIT
 *   5. begin TX3, log a payload for block 52, ABORT (simulates uncommitted
 *      work that survived to the journal but never reached commit)
 *   6. simulate dirty shutdown: do NOT apply the committed records to the
 *      data area, do NOT checkpoint
 *   7. mount_hook re-opens, observes needs_replay, and runs replay
 *   8. verify: data blocks 50, 51 now hold the committed payloads; data
 *      block 52 still has the pristine pattern (not applied)
 *   9. mount_hook a second time: needs_replay must now be 0 and cause
 *      must report NONE so a clean shutdown is recognized.
 */
static int test_dirty_shutdown_roundtrip_replays_only_committed(void) {
    struct mem_backend mem;
    struct block_device dev;
    struct journal j;
    struct journal_transaction txn;
    uint8_t pristine[JCAUSE_BLOCK_SIZE];
    uint8_t commit_a[JCAUSE_BLOCK_SIZE];
    uint8_t commit_b[JCAUSE_BLOCK_SIZE];
    uint8_t aborted_payload[JCAUSE_BLOCK_SIZE];
    uint8_t observed[JCAUSE_BLOCK_SIZE];
    int fails = 0;

    mem.block_size  = JCAUSE_BLOCK_SIZE;
    mem.block_count = JCAUSE_BLOCK_COUNT;
    mem.data = (uint8_t *)calloc(JCAUSE_BLOCK_COUNT, JCAUSE_BLOCK_SIZE);
    dev.name = "mem-roundtrip"; dev.block_size = JCAUSE_BLOCK_SIZE;
    dev.block_count = JCAUSE_BLOCK_COUNT; dev.ctx = &mem; dev.ops = &g_mem_ops;

    /* Pristine pattern + two distinct payloads. */
    memset(pristine, 0xA5u, sizeof(pristine));
    memset(commit_a, 0x11u, sizeof(commit_a));
    memset(commit_b, 0x22u, sizeof(commit_b));
    memset(aborted_payload, 0x33u, sizeof(aborted_payload));

    /* Step 1 + 2: format journal and seed the data area with the pristine
     * pattern via the raw backend. */
    fails += expect_true(journal_format(&j, &dev, JRNL_START, 32u) == 0,
                         "journal_format pre-step");
    fails += expect_true(mem_write(&mem, 50u, pristine) == 0, "seed blk 50");
    fails += expect_true(mem_write(&mem, 51u, pristine) == 0, "seed blk 51");
    fails += expect_true(mem_write(&mem, 52u, pristine) == 0, "seed blk 52");

    /* Step 3: TX1 commits a write for block 50. */
    fails += expect_true(journal_begin(&j, &txn) == 0, "tx1 begin");
    fails += expect_true(journal_log_block(&txn, 50u, commit_a, 0u,
                                           JCAUSE_BLOCK_SIZE) == 0,
                         "tx1 log");
    fails += expect_true(journal_commit(&txn) == 0, "tx1 commit");

    /* Step 4: TX2 commits a write for block 51. */
    fails += expect_true(journal_begin(&j, &txn) == 0, "tx2 begin");
    fails += expect_true(journal_log_block(&txn, 51u, commit_b, 0u,
                                           JCAUSE_BLOCK_SIZE) == 0,
                         "tx2 log");
    fails += expect_true(journal_commit(&txn) == 0, "tx2 commit");

    /* Step 5: TX3 logs work for block 52 but ABORTS — simulates a
     * transaction that never reached commit before power was lost. */
    fails += expect_true(journal_begin(&j, &txn) == 0, "tx3 begin");
    fails += expect_true(journal_log_block(&txn, 52u, aborted_payload, 0u,
                                           JCAUSE_BLOCK_SIZE) == 0,
                         "tx3 log");
    fails += expect_true(journal_abort(&txn) == 0, "tx3 abort");

    /* Step 6: simulate dirty shutdown. We do NOT call journal_checkpoint
     * and we do NOT manually apply the committed payloads to the data
     * area. From the device's point of view, blocks 50/51/52 still hold
     * the pristine pattern, but the journal contains durable commits. */
    fails += expect_true(mem_read(&mem, 50u, observed) == 0, "pre-replay read 50");
    fails += expect_true(memcmp(observed, pristine, JCAUSE_BLOCK_SIZE) == 0,
                         "blk 50 still pristine before replay");
    fails += expect_true(mem_read(&mem, 51u, observed) == 0, "pre-replay read 51");
    fails += expect_true(memcmp(observed, pristine, JCAUSE_BLOCK_SIZE) == 0,
                         "blk 51 still pristine before replay");

    /* Step 7: mount hook on the same backing store performs replay. */
    fails += expect_true(
        capyfs_journal_mount_hook(&dev, DATA_START, NULL, 0) == 0,
        "mount_hook after dirty shutdown succeeds");
    fails += expect_true(
        capyfs_journal_last_recovery_cause() == CAPYFS_JOURNAL_RECOVERY_WAL_REPLAY,
        "recovery cause must be WAL_REPLAY");

    /* Step 8: blocks 50 and 51 must now hold the committed payloads.
     * Block 52 must still be pristine because TX3 was aborted. */
    fails += expect_true(mem_read(&mem, 50u, observed) == 0, "post-replay read 50");
    fails += expect_true(memcmp(observed, commit_a, JCAUSE_BLOCK_SIZE) == 0,
                         "blk 50 must hold committed payload after replay");
    fails += expect_true(mem_read(&mem, 51u, observed) == 0, "post-replay read 51");
    fails += expect_true(memcmp(observed, commit_b, JCAUSE_BLOCK_SIZE) == 0,
                         "blk 51 must hold committed payload after replay");
    fails += expect_true(mem_read(&mem, 52u, observed) == 0, "post-replay read 52");
    fails += expect_true(memcmp(observed, pristine, JCAUSE_BLOCK_SIZE) == 0,
                         "blk 52 must remain pristine (TX3 aborted)");

    /* Step 9: a second mount_hook on the same device must observe a clean
     * journal and report NONE — the prior replay published a checkpoint. */
    fails += expect_true(
        capyfs_journal_mount_hook(&dev, DATA_START, NULL, 0) == 0,
        "second mount_hook must succeed");
    fails += expect_true(
        capyfs_journal_last_recovery_cause() == CAPYFS_JOURNAL_RECOVERY_NONE,
        "second mount must report cause NONE");

    /* Release the slot so subsequent tests can still allocate. The slot
     * table has CAPYFS_JOURNAL_MAX_MOUNTS=4 entries and there is no
     * automatic cleanup at process scope. */
    capyfs_journal_release_slot(&dev);
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
    fails += test_dirty_shutdown_roundtrip_replays_only_committed();
    fails += test_root_secret_install_clear();
    fails += test_authenticated_mount_with_super();
    if (fails == 0) {
        printf("[tests] capyfs_journal_cause OK\n");
    }
    return fails;
}
