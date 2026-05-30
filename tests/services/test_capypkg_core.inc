
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

static void test_full_sweep_can_continue_after_one_payload_failure(void) {
    reset_state(1);
    static char idx[2048];
    char hex[65];
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
             "install_root=/var/capypkg/bad.one\n---\n"
             "name=good.two\nversion=1.0.0\n"
             "payload_url=https://example.com/good-two.bin\n"
             "payload_sha256=%s\nsignature_ed25519=%s\n"
             "install_root=/var/capypkg/good.two\n---\n",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab",
             hex,
             "abababababababababababababababababababababababababababababababab"
             "abababababababababababababababababababababababababababababababab");
    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "multi-package index must populate catalog");
    g_payload_bytes = (const uint8_t *)CAPYPKG_TEST_PAYLOAD;
    g_payload_len = strlen(CAPYPKG_TEST_PAYLOAD);
    g_payload_rc = 0;
    g_payload_fail_url_substr = "bad-one";

    int rc1 = capypkg_install("good.one");
    int rc2 = capypkg_install("bad.one");
    int rc3 = capypkg_install("good.two");

    EXPECT(rc1 == CAPYPKG_OK, "first good package must install");
    EXPECT(rc2 == CAPYPKG_ERR_FETCH, "bad package must fail fetch");
    EXPECT(rc3 == CAPYPKG_OK,
           "package after a failed one must still install");
    EXPECT(capypkg_installed_count() == 2u,
           "failed package must not prevent later installs");
    struct capypkg_entry got;
    EXPECT(capypkg_installed_get("good.one", &got) == CAPYPKG_OK,
           "first good package must remain installed");
    EXPECT(capypkg_installed_get("good.two", &got) == CAPYPKG_OK,
           "second good package must remain installed");
}

