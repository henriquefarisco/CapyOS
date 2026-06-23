
/* ── workspace P0: CapyOS-side Ed25519 descriptor verifier (KAT) ───────── */

/* Frozen cross-repo known-answer vector (CapyAgent docs/compatibility.md
 * "Canonical descriptor known-answer vector"). The seed behind this public key
 * is publicly known, so this triple is TEST-ONLY: it proves the verifier slot +
 * the kernel ed25519_verify are wired correctly, and is never pinned as a
 * production trust anchor. */
static const char *const SIG_KAT_PUBKEY_HEX =
    "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
static const char *const SIG_KAT_DESCRIPTOR =
    "name=org.capyos.agent.core|version=1.2.3|payload_sha256="
    "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"
    "|payload_url=https://github.com/henriquefarisco/CapyAgent/releases/"
    "download/v1.2.3/org.capyos.agent.core-1.2.3.bin\n";
static const char *const SIG_KAT_SIGNATURE_HEX =
    "9788539478ef8b7d0a64339047a98f9a5833f7b069ef29d9a17cd25f8a664280"
    "6a80afe64708c4eece3d6d80eb3ebb415bedde868f5de01d9f8ae30a199c3d0a";

static int sig_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void sig_kat_pin_key(void) {
    uint8_t key[32];
    size_t i;
    for (i = 0u; i < 32u; ++i) {
        key[i] = (uint8_t)((sig_hexval(SIG_KAT_PUBKEY_HEX[2u * i]) << 4) |
                           sig_hexval(SIG_KAT_PUBKEY_HEX[2u * i + 1u]));
    }
    capypkg_set_trusted_publisher_key(key);
}

static void test_signature_descriptor_is_235_bytes(void) {
    EXPECT(strlen(SIG_KAT_DESCRIPTOR) == 235u,
           "KAT canonical descriptor must be exactly 235 bytes");
}

static void test_signature_verifies_kat(void) {
    sig_kat_pin_key();
    EXPECT(capypkg_ed25519_verify_signature(SIG_KAT_DESCRIPTOR,
                                            strlen(SIG_KAT_DESCRIPTOR),
                                            SIG_KAT_SIGNATURE_HEX) == 0,
           "valid KAT signature must verify against the pinned key");
    capypkg_clear_trusted_publisher_key();
}

static void test_signature_fail_closed_without_key(void) {
    capypkg_clear_trusted_publisher_key();
    EXPECT(capypkg_ed25519_verify_signature(SIG_KAT_DESCRIPTOR,
                                            strlen(SIG_KAT_DESCRIPTOR),
                                            SIG_KAT_SIGNATURE_HEX) != 0,
           "verifier must fail closed when no trusted key is pinned");
}

static void test_signature_rejects_tampered_descriptor(void) {
    char tampered[256];
    sig_kat_pin_key();
    strcpy(tampered, SIG_KAT_DESCRIPTOR);
    tampered[0] = 'N'; /* flip leading 'n' of "name=" */
    EXPECT(capypkg_ed25519_verify_signature(tampered, strlen(tampered),
                                            SIG_KAT_SIGNATURE_HEX) != 0,
           "a tampered descriptor must not verify");
    capypkg_clear_trusted_publisher_key();
}

static void test_signature_rejects_tampered_signature(void) {
    char badsig[129];
    sig_kat_pin_key();
    strcpy(badsig, SIG_KAT_SIGNATURE_HEX);
    badsig[0] = (badsig[0] == '9') ? '8' : '9'; /* flip first nibble */
    EXPECT(capypkg_ed25519_verify_signature(SIG_KAT_DESCRIPTOR,
                                            strlen(SIG_KAT_DESCRIPTOR),
                                            badsig) != 0,
           "a tampered signature must not verify");
    capypkg_clear_trusted_publisher_key();
}

static void test_signature_rejects_malformed_hex(void) {
    char shorthex[129];
    char longhex[160];
    sig_kat_pin_key();
    EXPECT(capypkg_ed25519_verify_signature(SIG_KAT_DESCRIPTOR,
                                            strlen(SIG_KAT_DESCRIPTOR),
                                            "zz") != 0,
           "non-hex signature must be rejected");
    strncpy(shorthex, SIG_KAT_SIGNATURE_HEX, 127u);
    shorthex[127] = '\0';
    EXPECT(capypkg_ed25519_verify_signature(SIG_KAT_DESCRIPTOR,
                                            strlen(SIG_KAT_DESCRIPTOR),
                                            shorthex) != 0,
           "short (127-hex) signature must be rejected");
    strcpy(longhex, SIG_KAT_SIGNATURE_HEX);
    strcat(longhex, "ab"); /* 130 hex chars */
    EXPECT(capypkg_ed25519_verify_signature(SIG_KAT_DESCRIPTOR,
                                            strlen(SIG_KAT_DESCRIPTOR),
                                            longhex) != 0,
           "over-long signature must be rejected");
    capypkg_clear_trusted_publisher_key();
}

/* End-to-end through the install gate with the REAL verifier registered. This
 * uniquely exercises build_signed_descriptor (capypkg_install.c): the manifest
 * carries the KAT name/version/payload_sha256/payload_url, the fake fetcher
 * returns "test" (whose sha256 IS the KAT payload_sha256), so the gate rebuilds
 * the canonical descriptor from the entry fields and the KAT signature must
 * verify -- locking the install-path descriptor format to the canonical KAT. */
static void test_install_real_verifier_accepts_kat(void) {
    static char idx[1024];
    reset_state(1);
    capypkg_set_signature_verifier(capypkg_ed25519_verify_signature);
    sig_kat_pin_key();
    snprintf(idx, sizeof(idx),
             "name=org.capyos.agent.core\nversion=1.2.3\n"
             "payload_url=https://github.com/henriquefarisco/CapyAgent/releases/"
             "download/v1.2.3/org.capyos.agent.core-1.2.3.bin\n"
             "payload_sha256=9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\n"
             "signature_ed25519=%s\n"
             "payload_size=4\ninstall_root=/var/capypkg/org.capyos.agent.core\n---\n",
             SIG_KAT_SIGNATURE_HEX);
    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "KAT manifest must populate the catalog");
    g_payload_bytes = (const uint8_t *)"test";
    g_payload_len = 4u;
    g_payload_rc = 0;
    EXPECT(capypkg_install("org.capyos.agent.core") == CAPYPKG_OK,
           "real verifier must accept the KAT-signed package end-to-end");
    EXPECT(capypkg_installed_count() == 1u,
           "KAT-signed package must be tracked as installed");
    capypkg_clear_trusted_publisher_key();
}

static void test_install_real_verifier_rejects_tampered_kat(void) {
    static char idx[1024];
    char badsig[129];
    reset_state(1);
    capypkg_set_signature_verifier(capypkg_ed25519_verify_signature);
    sig_kat_pin_key();
    strcpy(badsig, SIG_KAT_SIGNATURE_HEX);
    badsig[10] = (badsig[10] == 'a') ? 'b' : 'a'; /* still 128 hex, wrong sig */
    snprintf(idx, sizeof(idx),
             "name=org.capyos.agent.core\nversion=1.2.3\n"
             "payload_url=https://github.com/henriquefarisco/CapyAgent/releases/"
             "download/v1.2.3/org.capyos.agent.core-1.2.3.bin\n"
             "payload_sha256=9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\n"
             "signature_ed25519=%s\n"
             "payload_size=4\ninstall_root=/var/capypkg/org.capyos.agent.core\n---\n",
             badsig);
    g_index_text = idx;
    g_index_rc = 0;
    EXPECT(capypkg_fetch_index() == CAPYPKG_OK,
           "tampered-KAT manifest must populate the catalog");
    g_payload_bytes = (const uint8_t *)"test";
    g_payload_len = 4u;
    g_payload_rc = 0;
    EXPECT(capypkg_install("org.capyos.agent.core") == CAPYPKG_ERR_SIGNATURE,
           "real verifier must reject a tampered KAT signature at the install gate");
    EXPECT(capypkg_installed_count() == 0u,
           "rejected install must not be staged");
    capypkg_clear_trusted_publisher_key();
}
