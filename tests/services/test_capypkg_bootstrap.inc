static void test_vfs_write_requires_write_open_flag(void) {
    struct file *file = NULL;
    long written = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir("/tmp");
    fs_write_text("/tmp/file", "seed");

    file = vfs_open("/tmp/file", VFS_OPEN_READ);
    EXPECT(file != NULL, "read-only open must return a fake file");
    written = vfs_write(file, "x", 1u);
    EXPECT(written < 0, "read-only fake VFS handle must reject writes");
    EXPECT(vfs_last_error() == VFS_ERR_PERMISSION_DENIED,
           "read-only write must report permission denied");
    if (file) {
        vfs_close(file);
    }
}

static void test_vfs_write_appends_at_current_position(void) {
    struct file *file = NULL;
    char buffer[16];
    size_t len = 0u;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir("/tmp");
    fs_write_text("/tmp/file", "seed");

    file = vfs_open("/tmp/file", VFS_OPEN_WRITE);
    EXPECT(file != NULL, "write open must return a fake file");
    if (file) {
        file->position = 4u;
        EXPECT(vfs_write(file, "-x", 2u) == 2,
               "fake VFS write must honor current file position");
        EXPECT(file->position == 6u,
               "fake VFS write must advance file position");
        vfs_close(file);
    }
    EXPECT(fs_read("/tmp/file", buffer, sizeof(buffer), &len) == 0,
           "fake VFS written file must remain readable");
    EXPECT(strcmp(buffer, "seed-x") == 0,
           "fake VFS write must append at current position");
    EXPECT(len == 6u, "fake VFS read length must include appended bytes");
}

static void test_vfs_write_zero_bytes_is_noop(void) {
    struct file *file = NULL;
    char buffer[16];
    size_t len = 0u;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir("/tmp");
    fs_write_text("/tmp/file", "seed");

    file = vfs_open("/tmp/file", VFS_OPEN_WRITE);
    EXPECT(file != NULL, "zero-write open must return a fake file");
    if (file) {
        file->position = 2u;
        EXPECT(vfs_write(file, NULL, 0u) == 0,
               "fake VFS zero-byte write must succeed");
        EXPECT(file->position == 2u,
               "fake VFS zero-byte write must not advance position");
        vfs_close(file);
    }
    EXPECT(fs_read("/tmp/file", buffer, sizeof(buffer), &len) == 0,
           "zero-write file must remain readable");
    EXPECT(strcmp(buffer, "seed") == 0,
           "zero-byte write must not alter file contents");
    EXPECT(len == 4u, "zero-byte write must not alter file length");
}

static void test_bootstrap_partial_failure_is_retryable_without_marker(void) {
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
    snprintf(idx, sizeof(idx),
             "name=good.one\nversion=1.0.0\n"
             "payload_url=https://example.com/good-one.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/good.one\n---\n"
             "name=bad.one\nversion=1.0.0\n"
             "payload_url=https://example.com/bad-one.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/bad.one\n---\n",
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
    g_payload_fail_url_substr = "bad-one";

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "partial bootstrap must return retryable storage error");
    EXPECT(installed == 1, "successful package must remain installed");
    EXPECT(failed == 1, "failed package must be counted");
    EXPECT(!vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "partial bootstrap must not write done marker");

    g_payload_fail_url_substr = NULL;
    installed = 0;
    failed = 0;
    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "retry after transient package failure must complete");
    EXPECT(installed == 2, "retry must count already-installed and recovered packages");
    EXPECT(failed == 0, "successful retry must report zero failures");
    EXPECT(vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "completed retry must write done marker");
}

static void test_bootstrap_marker_write_failure_is_retryable(void) {
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
             "name=good.one\nversion=1.0.0\n"
             "payload_url=https://example.com/good-one.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/good.one\n---\n",
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
    g_vfs_write_fail_path = INSTALL_PROFILE_BOOTSTRAP_DONE;

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "marker write failure must be retryable");
    EXPECT(installed == 1, "package install must complete before marker failure");
    EXPECT(failed == 0, "marker write failure must not be a package failure");
    EXPECT(!vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "failed marker write must not leave partial marker");

    g_vfs_write_fail_path = NULL;
    installed = 0;
    failed = 0;
    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "retry after marker write failure must complete");
    EXPECT(installed == 1, "retry must count already-installed package");
    EXPECT(failed == 0, "successful marker retry must report zero failures");
    EXPECT(vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "successful marker retry must write marker");
}

static void test_bootstrap_marker_open_failure_is_retryable(void) {
    static char idx[1024];
    char hex[65];
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=good.one\nversion=1.0.0\n"
             "payload_url=https://example.com/good-one.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/good.one\n---\n",
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
    g_vfs_open_fail_path = INSTALL_PROFILE_BOOTSTRAP_DONE;

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "marker open failure must be retryable");
    EXPECT(installed == 1, "package install must complete before marker open failure");
    EXPECT(failed == 0, "marker open failure must not be a package failure");
    EXPECT(!vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "failed marker open must not leave partial marker");

    g_vfs_open_fail_path = NULL;
    installed = 0;
    failed = 0;
    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "retry after marker open failure must complete");
    EXPECT(installed == 1, "marker open retry must count already-installed package");
    EXPECT(failed == 0, "successful marker open retry must report zero failures");
    EXPECT(vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "successful marker open retry must write marker");
}

static void test_bootstrap_marker_create_failure_is_retryable(void) {
    static char idx[1024];
    char hex[65];
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=good.one\nversion=1.0.0\n"
             "payload_url=https://example.com/good-one.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/good.one\n---\n",
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
    g_vfs_create_fail_path = INSTALL_PROFILE_BOOTSTRAP_DONE;

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "marker create failure must be retryable");
    EXPECT(installed == 1, "package install must complete before marker create failure");
    EXPECT(failed == 0, "marker create failure must not be a package failure");
    EXPECT(!vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "failed marker create must not leave partial marker");

    g_vfs_create_fail_path = NULL;
    installed = 0;
    failed = 0;
    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "retry after marker create failure must complete");
    EXPECT(installed == 1, "marker create retry must count already-installed package");
    EXPECT(failed == 0, "successful marker create retry must report zero failures");
    EXPECT(vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "successful marker create retry must write marker");
}

static void test_bootstrap_basic_marker_write_failure_is_retryable(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    g_vfs_write_fail_path = INSTALL_PROFILE_BOOTSTRAP_DONE;

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "basic marker write failure must be retryable");
    EXPECT(installed == 0, "basic marker failure must not install packages");
    EXPECT(failed == 0, "basic marker failure must report zero package failures");
    EXPECT(!vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "basic marker failure must not leave partial marker");

    g_vfs_write_fail_path = NULL;
    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "retry after basic marker failure must complete");
    EXPECT(vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "basic marker retry must write marker");
}

static void test_bootstrap_basic_marker_open_failure_is_retryable(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    g_vfs_open_fail_path = INSTALL_PROFILE_BOOTSTRAP_DONE;

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "basic marker open failure must be retryable");
    EXPECT(installed == 0, "basic marker open failure must not install packages");
    EXPECT(failed == 0,
           "basic marker open failure must report zero package failures");
    EXPECT(!vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "basic marker open failure must remove partial marker");

    g_vfs_open_fail_path = NULL;
    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "retry after basic marker open failure must complete");
    EXPECT(vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "basic marker open retry must write marker");
}

static void test_bootstrap_basic_marker_create_failure_is_retryable(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    g_vfs_create_fail_path = INSTALL_PROFILE_BOOTSTRAP_DONE;

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "basic marker create failure must be retryable");
    EXPECT(installed == 0, "basic marker create failure must not install packages");
    EXPECT(failed == 0,
           "basic marker create failure must report zero package failures");
    EXPECT(!vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "basic marker create failure must not leave partial marker");

    g_vfs_create_fail_path = NULL;
    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "retry after basic marker create failure must complete");
    EXPECT(vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "basic marker create retry must write marker");
}

static void test_bootstrap_marker_create_already_fallback_completes(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    char buffer[32];
    size_t len = 0u;
    reset_state(1);
    reset_bootstrap_vfs_state();
    g_vfs_create_already_path = INSTALL_PROFILE_BOOTSTRAP_DONE;

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "marker create already fallback must complete");
    EXPECT(installed == 0, "marker create already fallback must not install packages");
    EXPECT(failed == 0,
           "marker create already fallback must report zero package failures");
    EXPECT(fs_read(INSTALL_PROFILE_BOOTSTRAP_DONE, buffer, sizeof(buffer),
                   &len) == 0 &&
               strcmp(buffer, "kind=basic\n") == 0,
           "marker create already fallback must write marker contents");
}

static void test_bootstrap_existing_marker_unlink_failure_is_retryable(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    struct vfs_stat st;
    char buffer[32];
    size_t len = 0u;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir(INSTALL_PROFILE_DIR);
    fs_write_text(INSTALL_PROFILE_BOOTSTRAP_DONE, "stale\n");
    g_vfs_unlink_fail_path = INSTALL_PROFILE_BOOTSTRAP_DONE;

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "existing marker unlink failure must be retryable");
    EXPECT(installed == 0, "marker unlink failure must not install packages");
    EXPECT(failed == 0, "marker unlink failure must not count package failures");
    EXPECT(vfs_stat_path(INSTALL_PROFILE_BOOTSTRAP_DONE, &st) == 0 &&
               (st.mode & VFS_MODE_FILE),
           "failed marker unlink must leave existing marker file");
    EXPECT(fs_read(INSTALL_PROFILE_BOOTSTRAP_DONE, buffer, sizeof(buffer),
                   &len) == 0 &&
               strcmp(buffer, "stale\n") == 0,
           "failed marker unlink must preserve existing marker contents");

    g_vfs_unlink_fail_path = NULL;
    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "retry after marker unlink failure must complete");
    EXPECT(fs_read(INSTALL_PROFILE_BOOTSTRAP_DONE, buffer, sizeof(buffer),
                   &len) == 0 &&
               strcmp(buffer, "kind=basic\n") == 0,
           "marker unlink retry must replace marker contents");
}

static void test_bootstrap_existing_marker_rmdir_failure_is_retryable(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    struct vfs_stat st;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir(INSTALL_PROFILE_BOOTSTRAP_DONE);
    g_vfs_rmdir_fail_path = INSTALL_PROFILE_BOOTSTRAP_DONE;

    rc = capypkg_bootstrap_run(0, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "existing marker rmdir failure must be retryable");
    EXPECT(installed == 0, "marker rmdir failure must not install packages");
    EXPECT(failed == 0, "marker rmdir failure must not count package failures");
    EXPECT(vfs_stat_path(INSTALL_PROFILE_BOOTSTRAP_DONE, &st) == 0 &&
               (st.mode & VFS_MODE_DIR),
           "failed marker rmdir must leave existing marker directory");

    g_vfs_rmdir_fail_path = NULL;
    rc = capypkg_bootstrap_run(0, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "retry after marker rmdir failure must complete");
    EXPECT(vfs_stat_path(INSTALL_PROFILE_BOOTSTRAP_DONE, &st) == 0 &&
               (st.mode & VFS_MODE_FILE),
           "marker rmdir retry must replace directory with marker file");
}

static void test_bootstrap_marker_creates_missing_install_dirs(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    struct vfs_stat st;
    reset_state(1);
    reset_bootstrap_vfs_state();
    g_vfs_write_fail_path = NULL;

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "basic bootstrap must complete when install dirs are missing");
    EXPECT(vfs_stat_path("/system", &st) == 0 && (st.mode & VFS_MODE_DIR),
           "bootstrap marker must create /system");
    EXPECT(vfs_stat_path(INSTALL_PROFILE_DIR, &st) == 0 &&
               (st.mode & VFS_MODE_DIR),
           "bootstrap marker must create /system/install");
    EXPECT(vfs_stat_path(INSTALL_PROFILE_BOOTSTRAP_DONE, &st) == 0 &&
               (st.mode & VFS_MODE_FILE),
           "bootstrap marker must create bootstrap.done file");
}

static void test_bootstrap_system_file_blocks_marker_dir_retryable(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    struct vfs_stat st;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_write_text("/system", "not-a-directory\n");

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "/system file must make marker directory creation retryable");
    EXPECT(installed == 0, "/system file must not install packages");
    EXPECT(failed == 0, "/system file must not count package failures");
    EXPECT(vfs_stat_path("/system", &st) == 0 && (st.mode & VFS_MODE_FILE),
           "bootstrap must not convert existing /system file into directory");
    EXPECT(vfs_stat_path(INSTALL_PROFILE_BOOTSTRAP_DONE, &st) != 0,
           "failed marker directory creation must not write marker");
}

static void test_bootstrap_install_dir_file_blocks_marker_retryable(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    struct vfs_stat st;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir("/system");
    fs_write_text(INSTALL_PROFILE_DIR, "not-a-directory\n");

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "/system/install file must make marker creation retryable");
    EXPECT(installed == 0, "/system/install file must not install packages");
    EXPECT(failed == 0, "/system/install file must not count package failures");
    EXPECT(vfs_stat_path(INSTALL_PROFILE_DIR, &st) == 0 &&
               (st.mode & VFS_MODE_FILE),
           "bootstrap must not convert existing /system/install file");
    EXPECT(vfs_stat_path(INSTALL_PROFILE_BOOTSTRAP_DONE, &st) != 0,
           "failed install dir creation must not write marker");
}

static void test_bootstrap_ignores_directory_named_done_marker(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    struct vfs_stat st;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir(INSTALL_PROFILE_BOOTSTRAP_DONE);

    rc = capypkg_bootstrap_run(0, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "directory named bootstrap.done must not short-circuit bootstrap");
    EXPECT(vfs_stat_path(INSTALL_PROFILE_BOOTSTRAP_DONE, &st) == 0 &&
               (st.mode & VFS_MODE_FILE),
           "bootstrap must replace directory marker with file marker");
}

static void test_bootstrap_nonempty_marker_directory_is_retryable(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    struct vfs_stat st;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir(INSTALL_PROFILE_BOOTSTRAP_DONE);
    fs_write_text("/system/install/bootstrap.done/child", "stale\n");

    rc = capypkg_bootstrap_run(0, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "non-empty bootstrap.done directory must be retryable storage");
    EXPECT(installed == 0, "non-empty marker directory must not install packages");
    EXPECT(failed == 0, "non-empty marker directory must not count package failures");
    EXPECT(vfs_stat_path(INSTALL_PROFILE_BOOTSTRAP_DONE, &st) == 0 &&
               (st.mode & VFS_MODE_DIR),
           "failed marker replacement must leave directory marker unresolved");
}

static void test_bootstrap_marker_file_short_circuits_bad_profile(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir(INSTALL_PROFILE_DIR);
    fs_write_text(INSTALL_PROFILE_BOOTSTRAP_DONE, "kind=basic\n");
    fs_mkdir(INSTALL_PROFILE_PATH);

    rc = capypkg_bootstrap_run(0, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_OK,
           "existing marker file must short-circuit unreadable profile");
    EXPECT(installed == 0, "marker no-op must report zero installs");
    EXPECT(failed == 0, "marker no-op must report zero failures");
}

static void test_bootstrap_force_bypasses_marker_file(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir(INSTALL_PROFILE_DIR);
    fs_write_text(INSTALL_PROFILE_BOOTSTRAP_DONE, "kind=basic\n");
    fs_mkdir(INSTALL_PROFILE_PATH);

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "force bootstrap must bypass marker and surface bad profile");
    EXPECT(installed == 0, "forced bad profile must not install packages");
    EXPECT(failed == 0, "forced bad profile must not count package failures");
}

static void test_bootstrap_profile_directory_is_retryable_storage(void) {
    int installed = 0;
    int failed = 0;
    int rc = 0;
    reset_state(1);
    reset_bootstrap_vfs_state();
    fs_mkdir(INSTALL_PROFILE_PATH);

    rc = capypkg_bootstrap_run(1, &installed, &failed);
    EXPECT(rc == INSTALL_PROFILE_ERR_STORAGE,
           "profile.ini directory must be reported as storage error");
    EXPECT(installed == 0, "unreadable profile must not install packages");
    EXPECT(failed == 0, "unreadable profile must not count package failures");
    EXPECT(!vfs_path_exists(INSTALL_PROFILE_BOOTSTRAP_DONE),
           "unreadable profile must not write bootstrap marker");
}
