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

#include "services/capypkg.h"
#include "security/ed25519.h"

#include <stddef.h>
#include <stdint.h>

/* Trusted publisher Ed25519 public key (32 bytes). Unset -> fail-closed. */
static uint8_t g_capypkg_trusted_pubkey[ED25519_PUBLIC_KEY_SIZE];
static int g_capypkg_trusted_pubkey_set = 0;

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
