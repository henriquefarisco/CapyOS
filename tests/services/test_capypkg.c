/*
 * tests/services/test_capypkg.c
 *
 * Host-side unit tests for the capypkg adapter. Covers:
 *   - init seeds the default repository
 *   - manifest parser accepts well-formed descriptors and rejects
 *     malformed/insecure ones (non-HTTPS URL, missing sha256)
 *   - repository add/remove respect HTTPS-only and pinned protection
 *   - install path verifies SHA-256 and rejects on mismatch
 *   - signature gate fails closed when no verifier is plugged and
 *     accepts when the injected verifier approves
 *   - install/update lifecycle drives state and persists DB
 */
#include "capypkg_test_harness.h"

/* ── tests ────────────────────────────────────────────────────────── */

static void test_init_seeds_default_repo(void) {
    reset_state(1);
    EXPECT(capypkg_initialized() == 1, "init must mark initialized");
    EXPECT(capypkg_repo_count() == 1u,
           "init must seed exactly one default repo");
    struct capypkg_repo repo;
    EXPECT(capypkg_repo_get_at(0u, &repo) == CAPYPKG_OK,
           "init must expose seeded repo");
    EXPECT(strcmp(repo.name, "stable") == 0,
           "seeded repo must be named 'stable'");
    EXPECT(repo.pinned == 1u, "seeded repo must be pinned");
    EXPECT(repo.require_signature == 1u,
           "seeded repo must require signature");
}

static void test_repo_add_requires_https(void) {
    reset_state(1);
    int rc = capypkg_repo_add("local", "http://example.com/index", 0);
    EXPECT(rc == CAPYPKG_ERR_DENIED,
           "non-https repo URL must be rejected");
    EXPECT(capypkg_repo_count() == 1u,
           "rejected repo must not be stored");
    rc = capypkg_repo_add("local", "https://example.com/index", 0);
    EXPECT(rc == CAPYPKG_OK, "https repo URL must be accepted");
    EXPECT(capypkg_repo_count() == 2u,
           "accepted repo must be stored");
}

static void test_repo_remove_protects_pinned(void) {
    reset_state(1);
    int rc = capypkg_repo_remove("stable");
    EXPECT(rc == CAPYPKG_ERR_DENIED,
           "pinned default repo must not be removable");
    EXPECT(capypkg_repo_count() == 1u,
           "pinned repo must remain after remove attempt");
    (void)capypkg_repo_add("extra", "https://repo.example/x", 0);
    rc = capypkg_repo_remove("extra");
    EXPECT(rc == CAPYPKG_OK, "non-pinned repo must be removable");
    EXPECT(capypkg_repo_count() == 1u,
           "remove must reduce repo count");
}

static void test_fetch_index_populates_catalog_signed_repo(void) {
    reset_state(1);
    static char index_buf[1024];
    char hex[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(index_buf, sizeof(index_buf),
             "name=foo\nversion=1.0.0\npayload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\nsummary=test\n---\n",
             hex,
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    g_index_text = index_buf;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    EXPECT(rc == CAPYPKG_OK, "fetch_index must succeed with HTTPS source");
    EXPECT(capypkg_available_count() == 1u,
           "fetch_index must populate one available entry");
    struct capypkg_entry got;
    EXPECT(capypkg_available_get("foo", &got) == CAPYPKG_OK,
           "fetched entry must be locatable by name");
    EXPECT(strcmp(got.version, "1.0.0") == 0,
           "fetched version must match descriptor");
}

static void test_install_rejects_non_https_payload(void) {
    reset_state(1);
    /* Manifest with HTTP payload_url should fail at parse time. */
    static const char idx[] =
        "name=foo\nversion=1.0.0\npayload_url=http://example.com/foo.bin\n"
        "payload_sha256=" "0000000000000000000000000000000000000000000000000000000000000000"
        "\n---\n";
    g_index_text = idx;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    /* Parser refuses every entry => fetch_index returns the last
     * negative rc which is parser-driven for non-https payloads. */
    EXPECT(rc != CAPYPKG_OK,
           "non-https payload URL must be refused by manifest parser");
    EXPECT(capypkg_available_count() == 0u,
           "rejected entry must not populate catalog");
}

static void test_install_verifies_sha256_and_signature(void) {
    reset_state(1);
    static char idx[1024];
    char hex[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\npayload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\ninstall_root=/var/capypkg/foo\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");

    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "manifest must populate catalog");

    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    /* signature verifier returns 0 => OK. */
    g_signature_rc = 0;

    int rc = capypkg_install("foo");
    EXPECT(rc == CAPYPKG_OK, "valid payload must install successfully");
    EXPECT(g_signature_calls == 1,
           "signature verifier must be called exactly once");
    EXPECT(capypkg_installed_count() == 1u,
           "installed package must be tracked");

    /* Re-install of same version should be ALREADY. */
    rc = capypkg_install("foo");
    EXPECT(rc == CAPYPKG_ERR_ALREADY,
           "duplicate install must report already-installed");
}

static void test_install_rejects_sha256_mismatch(void) {
    reset_state(1);
    static char idx[1024];
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\npayload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\ninstall_root=/var/capypkg/foo\n---\n",
             "1111111111111111111111111111111111111111111111111111111111111111",
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "manifest must populate catalog");
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_signature_rc = 0;
    int rc = capypkg_install("foo");
    EXPECT(rc == CAPYPKG_ERR_DIGEST,
           "sha256 mismatch must abort install with DIGEST error");
    EXPECT(capypkg_installed_count() == 0u,
           "failed install must not add to installed table");
}

static void test_install_fail_closed_without_verifier(void) {
    /* No verifier plugged in. Repo default requires signature. */
    reset_state(0);
    static char idx[1024];
    char hex[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\npayload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\ninstall_root=/var/capypkg/foo\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "manifest must populate catalog");
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    int rc = capypkg_install("foo");
    EXPECT(rc == CAPYPKG_ERR_SIGNATURE,
           "signed repo must fail closed without verifier");
    EXPECT(capypkg_installed_count() == 0u,
           "fail-closed install must not be staged");
}

static void test_install_no_source_when_no_repo(void) {
    reset_state(1);
    /* Remove the seeded repo by replacing it with a non-pinned one
     * we can drop. Pinned default cannot be removed directly. We
     * emulate "no source" by re-resetting the state and clearing the
     * repo array via capypkg_reset() + re-init not calling seed. */
    capypkg_reset();
    /* Re-bind adapters but do NOT init -> initialized=0 -> install
     * should refuse with NOT_READY. */
    bind_runtime_adapters(1);
    int rc = capypkg_install("foo");
    EXPECT(rc == CAPYPKG_ERR_NOT_READY,
           "uninitialized adapter must refuse with NOT_READY");
}

static void test_repo_serialize_roundtrip(void) {
    reset_state(1);
    char buf[512];
    size_t len = 0u;
    int rc = capypkg_repo_save();
    /* After reset_state, writer is bound => save should succeed. */
    EXPECT(rc == CAPYPKG_OK, "repo_save must succeed when writer bound");
    rc = fs_read("/system/capypkg/repos.cfg", buf, sizeof(buf), &len);
    EXPECT(rc == 0,
           "serialized repos must be readable from fake filesystem");
    EXPECT(strstr(buf, "stable|https://") != NULL,
           "serialized repos must contain the seeded default entry");
}

static void test_install_rejects_when_verifier_returns_failure(void) {
    reset_state(1);
    static char idx[1024];
    char hex[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\npayload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\ninstall_root=/var/capypkg/foo\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "manifest must populate catalog");
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    /* Verifier exists but rejects the signature. */
    g_signature_rc = -1;
    int rc = capypkg_install("foo");
    EXPECT(rc == CAPYPKG_ERR_SIGNATURE,
           "rejected signature must abort install with SIGNATURE error");
    EXPECT(g_signature_calls == 1,
           "verifier must be invoked exactly once even on rejection");
    EXPECT(capypkg_installed_count() == 0u,
           "rejected install must not populate installed table");
}

static void test_manifest_rejects_install_root_outside_allowed(void) {
    reset_state(1);
    static char idx[1024];
    char hex[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\npayload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\ninstall_root=/etc/foo\n---\n",
             hex);
    g_index_text = idx;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    EXPECT(rc != CAPYPKG_OK,
           "install_root outside allowed prefixes must be refused");
    EXPECT(capypkg_available_count() == 0u,
           "rejected descriptor must not populate catalog");
}

static void test_manifest_rejects_malformed_sha256(void) {
    reset_state(1);
    static const char idx[] =
        "name=foo\nversion=1.0.0\npayload_url=https://example.com/foo.bin\n"
        "payload_sha256=not-hex-at-all\n---\n";
    g_index_text = idx;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    EXPECT(rc != CAPYPKG_OK,
           "non-hex payload_sha256 must be refused");
    EXPECT(capypkg_available_count() == 0u,
           "malformed sha256 must not populate catalog");
}

static void test_install_resolves_dependency_chain(void) {
    reset_state(1);
    static char idx[2048];
    char hex[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=libfoo\nversion=1.0.0\npayload_url=https://example.com/libfoo.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\ninstall_root=/var/capypkg/libfoo\n"
             "---\n"
             "name=app\nversion=2.0.0\npayload_url=https://example.com/app.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\ninstall_root=/var/capypkg/app\n"
             "depends=libfoo\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab",
             hex,
             "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"
             "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");
    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "two-entry manifest must populate catalog");
    EXPECT(capypkg_available_count() == 2u,
           "catalog must hold both libfoo and app");

    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_signature_rc = 0;

    /* Installing the dependent should pull in the dependency first. */
    int rc = capypkg_install("app");
    EXPECT(rc == CAPYPKG_OK, "app install must succeed");
    EXPECT(capypkg_installed_count() == 2u,
           "dependency must be auto-installed alongside the dependent");
    struct capypkg_entry libfoo;
    EXPECT(capypkg_installed_get("libfoo", &libfoo) == CAPYPKG_OK,
           "libfoo must appear in installed table");
}

static void test_install_dependency_missing_in_catalog_fails(void) {
    reset_state(1);
    static char idx[1024];
    char hex[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=app\nversion=1.0.0\npayload_url=https://example.com/app.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\ninstall_root=/var/capypkg/app\n"
             "depends=missing-dep\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "manifest must populate catalog");
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_signature_rc = 0;
    int rc = capypkg_install("app");
    EXPECT(rc == CAPYPKG_ERR_DEPENDENCY,
           "missing dependency must abort install with DEPENDENCY error");
    EXPECT(capypkg_installed_count() == 0u,
           "failed dependency must not stage anything");
}

static void test_stats_reports_state_correctly(void) {
    reset_state(1);
    struct capypkg_stats stats;
    capypkg_stats_get(&stats);
    EXPECT(stats.initialized == 1u, "stats must show initialized state");
    EXPECT(stats.repo_count == 1u,
           "stats must reflect seeded repository count");
    EXPECT(stats.any_repo_signed == 1u,
           "stats must reflect signed default repo");
    EXPECT(stats.installed_count == 0u,
           "stats must report zero installed on fresh init");
    EXPECT(stats.updates_pending == 0u,
           "stats must report zero updates on fresh init");
}

static void test_install_emits_audit_trail_on_success(void) {
    reset_state(1);
    static char idx[1024];
    char hex[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\npayload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\ninstall_root=/var/capypkg/foo\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "manifest must populate catalog");
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_signature_rc = 0;
    int rc = capypkg_install("foo");
    EXPECT(rc == CAPYPKG_OK, "install must succeed");
    const char *trail = klog_serialize();
    EXPECT(trail != NULL, "klog must expose serialized trail");
    EXPECT(strstr(trail,
                  "[audit] [capypkg] payload-sha256 verified; "
                  "package installed") != NULL,
           "install success must emit verified-installed audit entry");
}

static void test_install_emits_audit_trail_on_sha256_mismatch(void) {
    reset_state(1);
    static char idx[1024];
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\npayload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\ninstall_root=/var/capypkg/foo\n---\n",
             "1111111111111111111111111111111111111111111111111111111111111111",
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "manifest must populate catalog");
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_signature_rc = 0;
    int rc = capypkg_install("foo");
    EXPECT(rc == CAPYPKG_ERR_DIGEST,
           "install must fail with DIGEST on sha256 mismatch");
    const char *trail = klog_serialize();
    EXPECT(strstr(trail,
                  "[audit] [capypkg] payload-sha256 mismatch; "
                  "install aborted") != NULL,
           "digest failure must emit mismatch audit entry");
}

/* Bug 1 regression: any_repo_signed must reflect the union of all
 * configured repositories. The bug was that capypkg_repo_add's
 * "update existing repo" path only set the flag to 1 when the
 * incoming require_signature was 1, but never cleared it back to 0
 * on a signed -> unsigned transition. */
static void test_repo_update_clears_any_repo_signed_when_last_signed(void) {
    reset_state(1);
    struct capypkg_stats stats;
    capypkg_stats_get(&stats);
    EXPECT(stats.any_repo_signed == 1u,
           "seeded stable repo must report any_repo_signed=1");
    /* Converting the only signed repo to unsigned must clear the
     * cached flag. The seeded `stable` repo is the only one in the
     * fresh state. */
    int rc = capypkg_repo_add("stable", "https://repo.capyos.org/v1/index.cap",
                              0 /* require_signature */);
    EXPECT(rc == CAPYPKG_OK,
           "updating the existing repo to unsigned must succeed");
    capypkg_stats_get(&stats);
    EXPECT(stats.any_repo_signed == 0u,
           "any_repo_signed must drop to 0 after the last signed repo "
           "transitions to unsigned");
}

/* Bug 9 regression: name field must reject characters that would
 * let an attacker escape the install root or produce malformed
 * filesystem entries. */
static void test_manifest_rejects_unsafe_package_name(void) {
    reset_state(1);
    /* Slash in name => attempted path traversal in build_payload_target. */
    static char idx_slash[1024];
    snprintf(idx_slash, sizeof(idx_slash),
             "name=../etc/passwd\nversion=1.0.0\n"
             "payload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\ninstall_root=/var/capypkg/foo\n---\n",
             "0000000000000000000000000000000000000000000000000000000000000000");
    g_index_text = idx_slash;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    EXPECT(rc != CAPYPKG_OK,
           "name containing '/' must be refused by the parser");
    EXPECT(capypkg_available_count() == 0u,
           "rejected manifest must not appear in the catalog");

    /* The bare ".." string is rejected even though every byte is in
     * the allowed alphabet, because it has special meaning on POSIX
     * filesystems. */
    reset_state(1);
    static char idx_dotdot[1024];
    snprintf(idx_dotdot, sizeof(idx_dotdot),
             "name=..\nversion=1.0.0\n"
             "payload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\ninstall_root=/var/capypkg/foo\n---\n",
             "0000000000000000000000000000000000000000000000000000000000000000");
    g_index_text = idx_dotdot;
    g_index_rc = 0;
    rc = capypkg_fetch_index();
    EXPECT(rc != CAPYPKG_OK,
           "name '..' must be refused even with all bytes in alphabet");

    /* '@' is outside the safe alphabet [a-zA-Z0-9._-]. */
    reset_state(1);
    static char idx_at[1024];
    snprintf(idx_at, sizeof(idx_at),
             "name=foo@latest\nversion=1.0.0\n"
             "payload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\ninstall_root=/var/capypkg/foo\n---\n",
             "0000000000000000000000000000000000000000000000000000000000000000");
    g_index_text = idx_at;
    g_index_rc = 0;
    rc = capypkg_fetch_index();
    EXPECT(rc != CAPYPKG_OK,
           "name containing '@' must be refused by the parser");
}

/* Bug 7 regression: install_root prefix check must require a
 * directory boundary, not bare starts-with. Previously
 * '/var/capypkgsneak' slipped past because it started with
 * '/var/capypkg'. */
static void test_manifest_rejects_install_root_prefix_bypass(void) {
    reset_state(1);
    static char idx[1024];
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\n"
             "payload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\ninstall_root=/var/capypkgsneak/foo\n---\n",
             "0000000000000000000000000000000000000000000000000000000000000000");
    g_index_text = idx;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    EXPECT(rc != CAPYPKG_OK,
           "install_root '/var/capypkgsneak/foo' must be refused (prefix "
           "bypass without directory boundary)");
    EXPECT(capypkg_available_count() == 0u,
           "rejected manifest must not appear in the catalog");
}

/* Bug 8 regression: install_root must reject '..' path segments
 * that would let payload writes escape the allowed roots. */
static void test_manifest_rejects_install_root_dotdot(void) {
    reset_state(1);
    static char idx[1024];
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\n"
             "payload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\ninstall_root=/var/capypkg/../etc\n---\n",
             "0000000000000000000000000000000000000000000000000000000000000000");
    g_index_text = idx;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    EXPECT(rc != CAPYPKG_OK,
           "install_root containing '/../' must be refused");
    EXPECT(capypkg_available_count() == 0u,
           "rejected manifest must not appear in the catalog");

    /* '..hidden' is NOT a dotdot segment (the second dot is not at
     * the segment boundary) and must remain accepted. */
    reset_state(1);
    static char idx_ok[1024];
    char hex_ok[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex_ok);
    snprintf(idx_ok, sizeof(idx_ok),
             "name=foo\nversion=1.0.0\n"
             "payload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\ninstall_root=/var/capypkg/..hidden\n---\n",
             hex_ok);
    g_index_text = idx_ok;
    g_index_rc = 0;
    rc = capypkg_fetch_index();
    EXPECT(rc == CAPYPKG_OK,
           "install_root containing '..hidden' (not a dotdot segment) "
           "must be accepted");
}

/* Bug 15 regression: when a single entry is malformed, the parser
 * must skip past it (advance to the next `---\n` separator) so the
 * surrounding loop can keep importing the entries that follow.
 * Previously a corrupt entry halted parsing and silently dropped
 * every subsequent valid descriptor — a DoS surface for remote
 * indexes. */
static void test_fetch_index_skips_malformed_entry_and_keeps_valid_one(void) {
    reset_state(1);
    static char idx[2048];
    char hex_ok[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex_ok);
    /* First entry is malformed: payload_size overflows uint32_t.
     * Second entry is structurally valid. The fix must report the
     * first entry's footprint via *consumed so the surrounding loop
     * advances past the trailing `---` and imports the second one. */
    snprintf(idx, sizeof(idx),
             /* bad entry */
             "name=bad\nversion=1.0.0\n"
             "payload_url=https://example.com/bad.bin\n"
             "payload_sha256=%s\npayload_size=99999999999\n"
             "install_root=/var/capypkg/bad\n---\n"
             /* good entry */
             "name=good\nversion=2.0.0\n"
             "payload_url=https://example.com/good.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/good\n---\n",
             hex_ok, hex_ok,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    g_index_text = idx;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    EXPECT(rc == CAPYPKG_OK,
           "fetch_index must succeed when at least one valid entry is "
           "imported");
    EXPECT(capypkg_available_count() == 1u,
           "the valid entry must be imported despite the leading "
           "malformed one");
    struct capypkg_entry entry;
    EXPECT(capypkg_available_get("good", &entry) == CAPYPKG_OK,
           "the surviving entry must be the second (valid) one");
    EXPECT(capypkg_available_get("bad", &entry) == CAPYPKG_ERR_NOT_FOUND,
           "the malformed entry must NOT appear in the catalog");
}

/* Bug 16 (repo path): pkg-source-add must also reject ANSI escape
 * bytes so a piped/scripted caller can never inject control chars
 * into repos.cfg, which is later echoed by pkg-source-list. */
static void test_repo_add_rejects_ansi_escape_in_arguments(void) {
    reset_state(1);
    /* index_url with embedded ESC. The string has to remain a valid
     * https:// prefix so it gets past the HTTPS gate first. */
    char url[64];
    const char *prefix = "https://attacker.com/";
    size_t i = 0u;
    for (; prefix[i]; ++i) url[i] = prefix[i];
    url[i++] = 0x1B;
    url[i++] = '[';
    url[i++] = '2';
    url[i++] = 'J';
    url[i] = '\0';
    int rc = capypkg_repo_add("evilrepo", url, 0);
    EXPECT(rc == CAPYPKG_ERR_DENIED,
           "pkg-source-add must refuse an index URL with ANSI escape");

    /* Name with embedded ESC. */
    char name[16];
    name[0] = 'b'; name[1] = 'a'; name[2] = 'd';
    name[3] = 0x1B;
    name[4] = '\0';
    rc = capypkg_repo_add(name, "https://attacker.com/", 0);
    EXPECT(rc == CAPYPKG_ERR_DENIED,
           "pkg-source-add must refuse a name with ANSI escape");
}

/* Bug 16 regression: manifest values are echoed to the serial port
 * by vga_write; an ANSI escape sequence inside `summary` (or any
 * other field) would be interpreted by a terminal emulator on the
 * other end and could forge fake prompts. Parser must reject any
 * non-printable byte up front. */
static void test_manifest_rejects_ansi_escape_in_value(void) {
    reset_state(1);
    static char idx[1024];
    char hex[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    /* Build the manifest manually so we can embed a raw ESC byte
     * (0x1B) into the summary field — snprintf "%s" stops at NUL but
     * happily copies ESC. */
    int prefix_len = snprintf(idx, sizeof(idx),
                              "name=foo\nversion=1.0.0\n"
                              "payload_url=https://example.com/foo.bin\n"
                              "payload_sha256=%s\nsummary=hello",
                              hex);
    if (prefix_len < 0 || (size_t)prefix_len + 32u > sizeof(idx)) {
        EXPECT(0, "manifest scaffold did not fit the buffer");
        return;
    }
    size_t pos = (size_t)prefix_len;
    /* Append literal "\x1b[2J" then close with newline + valid tail. */
    idx[pos++] = 0x1B;
    idx[pos++] = '[';
    idx[pos++] = '2';
    idx[pos++] = 'J';
    idx[pos++] = '\n';
    const char *tail = "install_root=/var/capypkg/foo\n---\n";
    for (size_t i = 0u; tail[i]; ++i) {
        idx[pos++] = tail[i];
    }
    idx[pos] = '\0';

    g_index_text = idx;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    EXPECT(rc != CAPYPKG_OK,
           "manifest with ANSI escape in summary must be refused");
    EXPECT(capypkg_available_count() == 0u,
           "rejected manifest must not appear in the catalog");
}

/* Bug 14 regression: dep names follow the same alphabet rule as
 * the top-level `name` field. Without parse-time validation, an
 * unsafe dep would be silently stored and would only surface as a
 * confusing CAPYPKG_ERR_DEPENDENCY at install time. */
static void test_manifest_rejects_unsafe_dependency_name(void) {
    reset_state(1);
    static char idx[1024];
    char hex[65];
    compute_sha256_hex((const uint8_t *)CAPYPKG_TEST_PAYLOAD,
                       strlen(CAPYPKG_TEST_PAYLOAD), hex);
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\n"
             "payload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\ndepends=../etc/passwd\n"
             "install_root=/var/capypkg/foo\n---\n",
             hex);
    g_index_text = idx;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    EXPECT(rc != CAPYPKG_OK,
           "dep name containing '/' must be refused at parse time");
    EXPECT(capypkg_available_count() == 0u,
           "rejected manifest must not appear in the catalog");
}

/* Bug 10 regression: payload_size must reject values that would
 * overflow uint32_t. Previously the silent wrap could bypass the
 * CAPYPKG_PAYLOAD_MAX quota check. */
static void test_manifest_rejects_payload_size_overflow(void) {
    reset_state(1);
    static char idx[1024];
    snprintf(idx, sizeof(idx),
             "name=foo\nversion=1.0.0\n"
             "payload_url=https://example.com/foo.bin\n"
             "payload_sha256=%s\npayload_size=99999999999\n"
             "install_root=/var/capypkg/foo\n---\n",
             "0000000000000000000000000000000000000000000000000000000000000000");
    g_index_text = idx;
    g_index_rc = 0;
    int rc = capypkg_fetch_index();
    EXPECT(rc != CAPYPKG_OK,
           "payload_size that overflows uint32_t must be refused");
    EXPECT(capypkg_available_count() == 0u,
           "rejected manifest must not appear in the catalog");
}

static void test_state_and_result_labels_are_well_formed(void) {
    EXPECT(strcmp(capypkg_state_label(CAPYPKG_STATE_INSTALLED), "installed") == 0,
           "state label for INSTALLED must be 'installed'");
    EXPECT(strcmp(capypkg_state_label(CAPYPKG_STATE_BROKEN), "broken") == 0,
           "state label for BROKEN must be 'broken'");
    EXPECT(strcmp(capypkg_result_label(CAPYPKG_OK), "ok") == 0,
           "result label for OK must be 'ok'");
    EXPECT(strcmp(capypkg_result_label(CAPYPKG_ERR_SIGNATURE),
                  "signature-mismatch") == 0,
           "result label for SIGNATURE must be 'signature-mismatch'");
    EXPECT(strcmp(capypkg_result_label(CAPYPKG_ERR_NO_SOURCE),
                  "no-repository-configured") == 0,
           "result label for NO_SOURCE must be 'no-repository-configured'");
}

/* ── runner entry ─────────────────────────────────────────────────── */

int run_capypkg_tests(void) {
    g_test_failures = 0;
    test_init_seeds_default_repo();
    test_repo_add_requires_https();
    test_repo_remove_protects_pinned();
    test_fetch_index_populates_catalog_signed_repo();
    test_install_rejects_non_https_payload();
    test_install_verifies_sha256_and_signature();
    test_install_rejects_sha256_mismatch();
    test_install_fail_closed_without_verifier();
    test_install_no_source_when_no_repo();
    test_repo_serialize_roundtrip();
    test_install_rejects_when_verifier_returns_failure();
    test_manifest_rejects_install_root_outside_allowed();
    test_manifest_rejects_malformed_sha256();
    test_install_resolves_dependency_chain();
    test_install_dependency_missing_in_catalog_fails();
    test_stats_reports_state_correctly();
    test_install_emits_audit_trail_on_success();
    test_install_emits_audit_trail_on_sha256_mismatch();
    test_repo_update_clears_any_repo_signed_when_last_signed();
    test_manifest_rejects_unsafe_package_name();
    test_manifest_rejects_install_root_prefix_bypass();
    test_manifest_rejects_install_root_dotdot();
    test_fetch_index_skips_malformed_entry_and_keeps_valid_one();
    test_repo_add_rejects_ansi_escape_in_arguments();
    test_manifest_rejects_ansi_escape_in_value();
    test_manifest_rejects_unsafe_dependency_name();
    test_manifest_rejects_payload_size_overflow();
    test_state_and_result_labels_are_well_formed();
    return g_test_failures;
}
