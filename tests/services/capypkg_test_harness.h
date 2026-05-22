/*
 * tests/services/capypkg_test_harness.h
 *
 * In-memory VFS/transport/signature adapters shared by the capypkg host-side
 * tests. This header is intentionally included by a single test translation
 * unit so the helpers can stay static and avoid leaking test symbols.
 */
#ifndef CAPYPKG_TEST_HARNESS_H
#define CAPYPKG_TEST_HARNESS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/log/klog.h"
#include "security/sha256.h"
#include "services/capypkg.h"

#define CAPYPKG_TEST_PAYLOAD "alpha-payload"

static int g_test_failures;

#define EXPECT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "[capypkg] %s\n", msg); \
            ++g_test_failures; \
        } \
    } while (0)

struct fake_file {
    char path[160];
    char text[2048];
    uint8_t bytes[2048];
    size_t bytes_len;
    int present;
    int is_text;
};

#define FAKE_FS_CAP 16
static struct fake_file g_fs[FAKE_FS_CAP];

static struct fake_file *fs_find(const char *path) {
    for (size_t i = 0u; i < FAKE_FS_CAP; ++i) {
        if (g_fs[i].present && strcmp(g_fs[i].path, path) == 0) {
            return &g_fs[i];
        }
    }
    return NULL;
}

static struct fake_file *fs_alloc(const char *path) {
    for (size_t i = 0u; i < FAKE_FS_CAP; ++i) {
        if (!g_fs[i].present) {
            memset(&g_fs[i], 0, sizeof(g_fs[i]));
            strncpy(g_fs[i].path, path, sizeof(g_fs[i].path) - 1u);
            g_fs[i].present = 1;
            return &g_fs[i];
        }
    }
    return NULL;
}

static void fs_reset(void) {
    memset(g_fs, 0, sizeof(g_fs));
}

static int fs_read(const char *path, char *buffer, size_t buffer_size,
                   size_t *out_len) {
    struct fake_file *f = fs_find(path);
    if (!f || !buffer || buffer_size < 2u) return -1;
    size_t len = strlen(f->text);
    if (len + 1u > buffer_size) len = buffer_size - 1u;
    memcpy(buffer, f->text, len);
    buffer[len] = '\0';
    if (out_len) *out_len = len;
    return 0;
}

static int fs_write_text(const char *path, const char *text) {
    struct fake_file *f = fs_find(path);
    if (!f) f = fs_alloc(path);
    if (!f) return -1;
    f->is_text = 1;
    strncpy(f->text, text ? text : "", sizeof(f->text) - 1u);
    f->text[sizeof(f->text) - 1u] = '\0';
    return 0;
}

static int fs_write_bytes(const char *path, const uint8_t *data, size_t len) {
    struct fake_file *f = fs_find(path);
    if (!f) f = fs_alloc(path);
    if (!f) return -1;
    f->is_text = 0;
    size_t stored = len > sizeof(f->bytes) ? sizeof(f->bytes) : len;
    if (data && stored > 0u) memcpy(f->bytes, data, stored);
    f->bytes_len = len;
    return 0;
}

static int fs_remove(const char *path) {
    struct fake_file *f = fs_find(path);
    if (!f) return 0;
    memset(f, 0, sizeof(*f));
    return 0;
}

static int fs_mkdir(const char *path) {
    (void)path;
    return 0;
}

static const char *g_index_text;
static int g_index_rc;
static const uint8_t *g_payload_bytes;
static size_t g_payload_len;
static int g_payload_rc;
static const char *g_payload_fail_url_substr;
static int g_signature_rc;
static int g_signature_calls;

static int net_fetch_text(const char *url, char *buffer, size_t buffer_size,
                          size_t *out_len) {
    (void)url;
    if (g_index_rc != 0 || !g_index_text) return -1;
    size_t len = strlen(g_index_text);
    if (len + 1u > buffer_size) len = buffer_size - 1u;
    memcpy(buffer, g_index_text, len);
    buffer[len] = '\0';
    if (out_len) *out_len = len;
    return 0;
}

static int net_fetch_bytes(const char *url, uint8_t *buffer, size_t buffer_size,
                           size_t *out_len) {
    if (g_payload_fail_url_substr && url &&
        strstr(url, g_payload_fail_url_substr) != NULL) {
        return -1;
    }
    if (g_payload_rc != 0 || !g_payload_bytes) return -1;
    size_t len = g_payload_len;
    if (len > buffer_size) len = buffer_size;
    memcpy(buffer, g_payload_bytes, len);
    if (out_len) *out_len = len;
    return 0;
}

static int signature_verifier_ok(const char *text, size_t len,
                                 const char *sig_hex) {
    (void)text;
    (void)len;
    (void)sig_hex;
    ++g_signature_calls;
    return g_signature_rc;
}

static void bind_runtime_adapters(int with_verifier) {
    capypkg_set_reader(fs_read);
    capypkg_set_writer(fs_write_text);
    capypkg_set_bytes_writer(fs_write_bytes);
    capypkg_set_remover(fs_remove);
    capypkg_set_mkdir(fs_mkdir);
    capypkg_set_text_fetcher(net_fetch_text);
    capypkg_set_bytes_fetcher(net_fetch_bytes);
    if (with_verifier) {
        capypkg_set_signature_verifier(signature_verifier_ok);
    } else {
        capypkg_set_signature_verifier(NULL);
    }
}

static void reset_state(int with_verifier) {
    fs_reset();
    g_index_text = NULL;
    g_index_rc = -1;
    g_payload_bytes = NULL;
    g_payload_len = 0u;
    g_payload_rc = -1;
    g_payload_fail_url_substr = NULL;
    g_signature_rc = 0;
    g_signature_calls = 0;
    klog_reset();
    capypkg_reset();
    bind_runtime_adapters(with_verifier);
    capypkg_init();
}

static void compute_sha256_hex(const uint8_t *data, size_t len,
                               char hex_out[65]) {
    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256_hash(data, len, digest);
    sha256_hex(digest, hex_out);
    hex_out[64] = '\0';
}

#endif /* CAPYPKG_TEST_HARNESS_H */
