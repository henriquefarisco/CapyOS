/*
 * tests/services/test_capypkg_bootstrap_sweep.inc
 *
 * First-boot install-sweep coverage, split out of
 * test_capypkg_bootstrap.inc (preventive split at the 900-line layout
 * ceiling). Covers the behaviour the bootstrap layers on top of the
 * per-package install:
 *   - per-package retry on transient errors (recover + cap at 3);
 *   - the dependency-ordered wave planner (dependency-before-dependent
 *     reorder, transitive CUSTOM dependency pull-in, cycle fail-closed).
 *
 * Like the sibling fragments, this is #included into
 * tests/services/test_capypkg.c under CAPYPKG_BOOTSTRAP_TESTS; the
 * TEST/EXPECT macros, the fake VFS/transport harness and the reset
 * helpers come from capypkg_test_harness.h + test_capypkg_vfs_stub.inc.
 */

static void test_bootstrap_per_package_retry_recovers(void) {
    static char idx[1024];
    char hex[65];
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    g_vfs_write_fail_path = NULL;
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=retry.one\nversion=1.0.0\n"
             "payload_url=https://example.com/retry-one.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/retry.one\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    fs_mkdir(INSTALL_PROFILE_DIR);
    fs_write_text(INSTALL_PROFILE_PATH,
                  "profile=full\n"
                  "bootstrap_repo_name=stable\n"
                  "bootstrap_repo_url=https://example.com/modules-index.txt\n"
                  "bootstrap_repo_signed=0\n");
    g_index_text = idx;
    g_index_rc = 0;
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_payload_fail_remaining = 2; /* fail twice, succeed on the third try */

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "per-package retry must recover after transient fetch failures");
    EXPECT(installed == 1, "recovered package must be counted installed");
    EXPECT(failed == 0, "recovered package must not be counted as failed");
    EXPECT(g_payload_fetch_calls == 3,
           "per-package retry must attempt the fetch exactly three times");
    EXPECT(vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "recovered bootstrap must write the done marker");
}

static void test_bootstrap_per_package_retry_caps_attempts(void) {
    static char idx[1024];
    char hex[65];
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    g_vfs_write_fail_path = NULL;
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=retry.one\nversion=1.0.0\n"
             "payload_url=https://example.com/retry-one.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/retry.one\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    fs_mkdir(INSTALL_PROFILE_DIR);
    fs_write_text(INSTALL_PROFILE_PATH,
                  "profile=full\n"
                  "bootstrap_repo_name=stable\n"
                  "bootstrap_repo_url=https://example.com/modules-index.txt\n"
                  "bootstrap_repo_signed=0\n");
    g_index_text = idx;
    g_index_rc = 0;
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_payload_fail_remaining = 9; /* always fails within the retry budget */

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "exhausted per-package retries must surface retryable storage error");
    EXPECT(installed == 0, "permanently failing package must not install");
    EXPECT(failed == 1, "permanently failing package must count once as failed");
    EXPECT(g_payload_fetch_calls == 3,
           "per-package retry must cap the fetch attempts at three");
    EXPECT(!vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "failed bootstrap must not write the done marker");
}

/* ---- dependency-ordered wave planner coverage --------------------------
 *
 * The bootstrap install order is observable through PACKAGE_BEGIN events,
 * so these tests assert the dependency-ordered wave planner: it installs a
 * dependency before its dependent (even when the catalog lists them in the
 * opposite order), pulls in transitive dependencies for a CUSTOM profile,
 * and handles a dependency cycle without hanging (fail-closed). ---- */

#define BOOTSTRAP_BEGIN_ORDER_MAX 16
static char g_bootstrap_begin_order[BOOTSTRAP_BEGIN_ORDER_MAX][CAPYPKG_NAME_MAX];
static int g_bootstrap_begin_count;

static void bootstrap_order_progress(enum capypkg_bootstrap_event event,
                                     const char *name, int index, int total,
                                     int rc, void *ctx) {
    (void)index;
    (void)total;
    (void)rc;
    (void)ctx;
    if (event == CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_BEGIN && name &&
        g_bootstrap_begin_count < BOOTSTRAP_BEGIN_ORDER_MAX) {
        strncpy(g_bootstrap_begin_order[g_bootstrap_begin_count], name,
                CAPYPKG_NAME_MAX - 1u);
        g_bootstrap_begin_order[g_bootstrap_begin_count][CAPYPKG_NAME_MAX - 1u] =
            '\0';
        g_bootstrap_begin_count++;
    }
}

static int bootstrap_begin_index_of(const char *name) {
    int i;
    for (i = 0; i < g_bootstrap_begin_count; ++i) {
        if (strcmp(g_bootstrap_begin_order[i], name) == 0) return i;
    }
    return -1;
}

static void test_bootstrap_installs_dependency_before_dependent(void) {
    static char idx[2048];
    char hex[65];
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    g_vfs_write_fail_path = NULL;
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    /* Catalog lists the DEPENDENT (app) before its dependency (libfoo);
     * raw catalog order would install app first, so a correct result
     * proves the wave planner reordered libfoo ahead of app. */
    snprintf(idx, sizeof(idx),
             "name=app\nversion=1.0.0\n"
             "payload_url=https://example.com/app.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/app\ndepends=libfoo\n---\n"
             "name=libfoo\nversion=1.0.0\n"
             "payload_url=https://example.com/libfoo.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/libfoo\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    fs_mkdir(INSTALL_PROFILE_DIR);
    fs_write_text(INSTALL_PROFILE_PATH,
                  "profile=full\n"
                  "bootstrap_repo_name=stable\n"
                  "bootstrap_repo_url=https://example.com/modules-index.txt\n"
                  "bootstrap_repo_signed=0\n");
    g_index_text = idx;
    g_index_rc = 0;
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_bootstrap_begin_count = 0;

    rc = capypkg_bootstrap_run_with_progress(1, &installed, &failed,
                                             bootstrap_order_progress, NULL);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "dependency-ordered sweep must complete");
    EXPECT(installed == 2 && failed == 0,
           "both dependency and dependent must install");
    EXPECT(bootstrap_begin_index_of("libfoo") >= 0 &&
               bootstrap_begin_index_of("app") >= 0 &&
               bootstrap_begin_index_of("libfoo") <
                   bootstrap_begin_index_of("app"),
           "wave planner must begin the dependency before the dependent");
}

static void test_bootstrap_custom_pulls_transitive_dependency(void) {
    static char idx[2048];
    char hex[65];
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    g_vfs_write_fail_path = NULL;
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    /* CUSTOM selects only "app"; the planner must pull in its catalog
     * dependency libfoo transitively and still order it first. */
    snprintf(idx, sizeof(idx),
             "name=app\nversion=1.0.0\n"
             "payload_url=https://example.com/app.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/app\ndepends=libfoo\n---\n"
             "name=libfoo\nversion=1.0.0\n"
             "payload_url=https://example.com/libfoo.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/libfoo\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    fs_mkdir(INSTALL_PROFILE_DIR);
    fs_write_text(INSTALL_PROFILE_PATH,
                  "profile=custom\n"
                  "bootstrap_repo_name=stable\n"
                  "bootstrap_repo_url=https://example.com/modules-index.txt\n"
                  "bootstrap_repo_signed=0\n"
                  "bootstrap_install=app\n");
    g_index_text = idx;
    g_index_rc = 0;
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_bootstrap_begin_count = 0;

    rc = capypkg_bootstrap_run_with_progress(1, &installed, &failed,
                                             bootstrap_order_progress, NULL);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "custom sweep with a transitive dependency must complete");
    EXPECT(installed == 2 && failed == 0,
           "custom profile must pull in the transitive dependency libfoo");
    EXPECT(bootstrap_begin_index_of("libfoo") >= 0 &&
               bootstrap_begin_index_of("app") >= 0 &&
               bootstrap_begin_index_of("libfoo") <
                   bootstrap_begin_index_of("app"),
           "transitive dependency must be installed before the selected package");
}

static void test_bootstrap_dependency_cycle_does_not_hang(void) {
    static char idx[2048];
    char hex[65];
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    g_vfs_write_fail_path = NULL;
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    /* a.pkg <-> b.pkg form an unsatisfiable dependency cycle. The planner
     * must place both in a final wave (no infinite loop) and the sweep
     * must fail closed (neither installs). */
    snprintf(idx, sizeof(idx),
             "name=a.pkg\nversion=1.0.0\n"
             "payload_url=https://example.com/a.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/a.pkg\ndepends=b.pkg\n---\n"
             "name=b.pkg\nversion=1.0.0\n"
             "payload_url=https://example.com/b.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/b.pkg\ndepends=a.pkg\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    fs_mkdir(INSTALL_PROFILE_DIR);
    fs_write_text(INSTALL_PROFILE_PATH,
                  "profile=full\n"
                  "bootstrap_repo_name=stable\n"
                  "bootstrap_repo_url=https://example.com/modules-index.txt\n"
                  "bootstrap_repo_signed=0\n");
    g_index_text = idx;
    g_index_rc = 0;
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_bootstrap_begin_count = 0;

    rc = capypkg_bootstrap_run_with_progress(1, &installed, &failed,
                                             bootstrap_order_progress, NULL);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "an unsatisfiable dependency cycle must be a retryable storage error");
    EXPECT(installed == 0, "cyclic packages must not install");
    EXPECT(failed == 2, "both packages in the cycle must be attempted and fail");
    EXPECT(g_bootstrap_begin_count == 2,
           "cycle handling must attempt every package without hanging");
    EXPECT(!vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "failed cyclic sweep must not write the done marker");
}
