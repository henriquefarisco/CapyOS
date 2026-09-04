/*
 * src/services/capypkg/capypkg_signature.c — CapyOS-side Ed25519 verifier for
 * signed capypkg descriptors (workspace P0).
 *
 * Bridges the capypkg signature gate (capypkg_install.c::verify_signature_if_
 * required) to the kernel's audited Ed25519 primitive (security/ed25519.c).
 * `capypkg_ed25519_verify_signature` matches `capypkg_verify_signature_fn` and
 * is registered via `capypkg_set_signature_verifier` in the kernel binder.
 *
 * SECURITY / fail-closed posture:
 *   - The trusted publisher public key is *unset by default*. With no key
 *     pinned, the verifier returns -1, so signed repositories still fail closed
 *     with CAPYPKG_ERR_SIGNATURE exactly as before this slice. Registering this
 *     verifier therefore changes NO production behaviour; it only puts the real
 *     verification machinery in place, host-tested against the frozen cross-repo
 *     KAT, ready for an operator to pin the official offline-generated key via
 *     capypkg_set_trusted_publisher_key().
 *   - The publicly-known KAT test key is NEVER pinned here (its seed is public);
 *     pinning it would let anyone sign packages. Promotion to a user-facing
 *     signed repo requires the real release key, per the signature policy.
 *   - Every error path (no key, NULL input, malformed/short/long hex, verify
 *     failure) returns non-zero. The signature hex must be exactly 128 lowercase
 *     or uppercase hex digits (64 bytes), matching the manifest format.
 */

#include "internal/capypkg_internal.h"
#include "security/capypkg_publisher_key.h"
#include "security/ed25519.h"
#include "security/sha256.h"

#include <stddef.h>
#include <stdint.h>

/* Trusted publisher Ed25519 public key (32 bytes). Unset -> fail-closed. */
static uint8_t g_capypkg_trusted_pubkey[ED25519_PUBLIC_KEY_SIZE];
static int g_capypkg_trusted_pubkey_set = 0;
static const uint8_t g_capypkg_production_pubkey[ED25519_PUBLIC_KEY_SIZE] =
    CAPYPKG_PUBLISHER_PUBLIC_KEY_BYTES;

void capypkg_use_production_publisher_key(void) {
    capypkg_set_trusted_publisher_key(g_capypkg_production_pubkey);
}

void capypkg_clear_trusted_publisher_key(void) {
    for (size_t i = 0u; i < (size_t)ED25519_PUBLIC_KEY_SIZE; ++i) {
        g_capypkg_trusted_pubkey[i] = 0u;
    }
    g_capypkg_trusted_pubkey_set = 0;
}

void capypkg_set_trusted_publisher_key(const uint8_t *key) {
    if (!key) {
        capypkg_clear_trusted_publisher_key();
        return;
    }
    for (size_t i = 0u; i < (size_t)ED25519_PUBLIC_KEY_SIZE; ++i) {
        g_capypkg_trusted_pubkey[i] = key[i];
    }
    g_capypkg_trusted_pubkey_set = 1;
}

/* Single hex nibble -> 0..15, or -1 for any non-hex byte (incl. NUL). */
static int capypkg_sig_hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Decode exactly out_len*2 hex chars (then a NUL) into out. Fail-closed:
 * returns -1 on NULL, a non-hex byte, a string shorter than expected, or any
 * trailing byte after the final pair. Reads no byte past the terminating NUL. */
static int capypkg_sig_decode_hex(const char *hex, uint8_t *out, size_t out_len) {
    if (!hex || !out) {
        return -1;
    }
    for (size_t i = 0u; i < out_len; ++i) {
        char c0 = hex[2u * i];
        if (c0 == '\0') {
            return -1; /* string too short; do not read past the NUL */
        }
        int hi = capypkg_sig_hex_nibble(c0);
        int lo = capypkg_sig_hex_nibble(hex[2u * i + 1u]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    if (hex[2u * out_len] != '\0') {
        return -1; /* trailing junk after the expected length */
    }
    return 0;
}

int capypkg_ed25519_verify_signature(const char *signed_text, size_t signed_len,
                                     const char *signature_hex) {
    uint8_t sig[ED25519_SIGNATURE_SIZE];
    if (!g_capypkg_trusted_pubkey_set) {
        return -1; /* fail-closed: no trust anchor pinned */
    }
    if (!signed_text || !signature_hex) {
        return -1;
    }
    if (capypkg_sig_decode_hex(signature_hex, sig, sizeof(sig)) != 0) {
        return -1;
    }
    if (ed25519_verify(sig, (const uint8_t *)signed_text, signed_len,
                       g_capypkg_trusted_pubkey) != 0) {
        return -1;
    }
    return 0;
}

static int index_line_value(const char *text, size_t len, size_t *cursor,
                            const char *prefix, char *out, size_t out_size) {
    size_t p = 0u, start, span;
    if (!text || !cursor || !prefix || !out || out_size == 0u) return -1;
    while (prefix[p]) {
        if (*cursor + p >= len || text[*cursor + p] != prefix[p]) return -1;
        ++p;
    }
    start = *cursor + p;
    *cursor = start;
    while (*cursor < len && text[*cursor] != '\n') ++(*cursor);
    if (*cursor >= len) return -1;
    span = *cursor - start;
    if (span == 0u || span + 1u > out_size) return -1;
    for (size_t i = 0u; i < span; ++i) out[i] = text[start + i];
    out[span] = '\0';
    ++(*cursor);
    return 0;
}

static int index_parse_u32(const char *text, uint32_t *out) {
    uint32_t value = 0u;
    size_t i = 0u;
    if (!text || !text[0] || !out) return -1;
    while (text[i]) {
        uint32_t digit;
        if (text[i] < '0' || text[i] > '9') return -1;
        digit = (uint32_t)(text[i] - '0');
        if (value > (0xffffffffu - digit) / 10u) return -1;
        value = value * 10u + digit;
        ++i;
    }
    *out = value;
    return 0;
}

int capypkg_verify_index_envelope(const char *text, size_t len,
                                  const char **body, size_t *body_len) {
    size_t cursor = 0u;
    char token[CAPYPKG_NAME_MAX], epoch_text[16];
    char body_sha[CAPYPKG_SHA256_HEX_MAX], actual_sha[CAPYPKG_SHA256_HEX_MAX];
    char signature[CAPYPKG_SIG_HEX_MAX], descriptor[256];
    uint8_t digest[SHA256_DIGEST_SIZE];
    uint32_t epoch = 0u;
    size_t descriptor_len;
    const char format_line[] = "#capyos-modules-index-v2\n";
    if (!text || !body || !body_len || len <= sizeof(format_line) - 1u) {
        return CAPYPKG_ERR_INDEX_TRUST;
    }
    for (size_t i = 0u; i < sizeof(format_line) - 1u; ++i) {
        if (text[i] != format_line[i]) return CAPYPKG_ERR_INDEX_TRUST;
    }
    cursor = sizeof(format_line) - 1u;
    if (index_line_value(text, len, &cursor, "#index_abi_token=", token,
                         sizeof(token)) != 0 ||
        index_line_value(text, len, &cursor, "#index_epoch=", epoch_text,
                         sizeof(epoch_text)) != 0 ||
        index_line_value(text, len, &cursor, "#index_body_sha256=", body_sha,
                         sizeof(body_sha)) != 0 ||
        index_line_value(text, len, &cursor, "#index_signature_ed25519=",
                         signature, sizeof(signature)) != 0) {
        return CAPYPKG_ERR_INDEX_TRUST;
    }
    if (!capypkg_local_equal(token, CAPYPKG_ABI_TOKEN) ||
        index_parse_u32(epoch_text, &epoch) != 0 ||
        epoch != CAPYPKG_INDEX_EPOCH ||
        !capypkg_local_hex_string_valid(body_sha, CAPYPKG_SHA256_HEX_LEN) ||
        !capypkg_local_hex_string_valid(signature, CAPYPKG_SIG_HEX_LEN) ||
        cursor >= len || !g_capypkg_signature_verifier) {
        return CAPYPKG_ERR_INDEX_TRUST;
    }
    sha256_hash((const uint8_t *)&text[cursor], len - cursor, digest);
    sha256_hex(digest, actual_sha);
    if (!capypkg_local_equal(actual_sha, body_sha)) {
        return CAPYPKG_ERR_INDEX_TRUST;
    }
    descriptor[0] = '\0';
    capypkg_local_append(descriptor, sizeof(descriptor), "format=");
    capypkg_local_append(descriptor, sizeof(descriptor), CAPYPKG_INDEX_FORMAT);
    capypkg_local_append(descriptor, sizeof(descriptor), "|abi_token=");
    capypkg_local_append(descriptor, sizeof(descriptor), token);
    capypkg_local_append(descriptor, sizeof(descriptor), "|epoch=");
    capypkg_local_append(descriptor, sizeof(descriptor), epoch_text);
    capypkg_local_append(descriptor, sizeof(descriptor), "|body_sha256=");
    capypkg_local_append(descriptor, sizeof(descriptor), body_sha);
    capypkg_local_append(descriptor, sizeof(descriptor), "\n");
    descriptor_len = capypkg_local_len(descriptor);
    if (g_capypkg_signature_verifier(descriptor, descriptor_len, signature) != 0) {
        return CAPYPKG_ERR_INDEX_TRUST;
    }
    *body = &text[cursor];
    *body_len = len - cursor;
    return CAPYPKG_OK;
}
