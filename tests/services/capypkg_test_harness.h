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
    int is_dir;
};

#define FAKE_FS_CAP 48
static struct fake_file g_fs[FAKE_FS_CAP];

static struct fake_file *fs_find(const char *path) {
    size_t i = 0u;
    if (!path) return NULL;
    for (i = 0u; i < FAKE_FS_CAP; ++i) {
        if (g_fs[i].present && strcmp(g_fs[i].path, path) == 0) {
            return &g_fs[i];
        }
    }
    return NULL;
}

static int fs_has_child(const char *path) {
    size_t len = 0u;
    size_t i = 0u;
    if (!path) return 0;
    len = strlen(path);
    if (len == 0u) return 0;
    for (i = 0u; i < FAKE_FS_CAP; ++i) {
        if (g_fs[i].present &&
            strncmp(g_fs[i].path, path, len) == 0 &&
            g_fs[i].path[len] == '/') {
            return 1;
        }
    }
    return 0;
}

static int fs_parent_dir_exists(const char *path) {
    char parent[160];
    size_t len = 0u;
    size_t split = 0u;
    struct fake_file *dir = NULL;
    if (!path || path[0] != '/') return 0;
    len = strlen(path);
    if (len == 0u || len >= sizeof(parent)) return 0;
    split = len;
    while (split > 1u && path[split - 1u] != '/') --split;
    if (split <= 1u) return 1;
    memcpy(parent, path, split - 1u);
    parent[split - 1u] = '\0';
    dir = fs_find(parent);
    return dir && dir->is_dir ? 1 : 0;
}

static struct fake_file *fs_alloc(const char *path) {
    size_t i = 0u;
    if (!path) return NULL;
    for (i = 0u; i < FAKE_FS_CAP; ++i) {
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
    size_t len = 0u;
    if (!f || f->is_dir || !buffer || buffer_size < 2u) return -1;
    len = strlen(f->text);
    if (len + 1u > buffer_size) len = buffer_size - 1u;
    memcpy(buffer, f->text, len);
    buffer[len] = '\0';
    if (out_len) *out_len = len;
    return 0;
}

static int fs_write_text(const char *path, const char *text) {
    struct fake_file *f = fs_find(path);
    if (!path || !text) return -1;
    if (!fs_parent_dir_exists(path)) return -1;
    if (f && f->is_dir) return -1;
    if (!f) f = fs_alloc(path);
    if (!f) return -1;
    f->is_dir = 0;
    f->is_text = 1;
    f->bytes_len = 0u;
    strncpy(f->text, text, sizeof(f->text) - 1u);
    f->text[sizeof(f->text) - 1u] = '\0';
    return 0;
}

static int fs_write_bytes(const char *path, const uint8_t *data, size_t len) {
    struct fake_file *f = fs_find(path);
    size_t stored = 0u;
    if (!path || (!data && len > 0u)) return -1;
    if (!fs_parent_dir_exists(path)) return -1;
    if (f && f->is_dir) return -1;
    if (!f) f = fs_alloc(path);
    if (!f) return -1;
    f->is_dir = 0;
    f->is_text = 0;
    stored = len > sizeof(f->bytes) ? sizeof(f->bytes) : len;
    if (data && stored > 0u) memcpy(f->bytes, data, stored);
    f->bytes_len = len;
    return 0;
}

static int fs_remove(const char *path) {
    struct fake_file *f = fs_find(path);
    if (!path) return -1;
    if (!f) return 0;
    if (f->is_dir) return -1;
    memset(f, 0, sizeof(*f));
    return 0;
}

static int fs_mkdir(const char *path) {
    char build[160];
    size_t build_len = 0u;
    const char *p = path;
    const char *start = NULL;
    size_t len = 0u;
    struct fake_file *f = NULL;
    struct fake_file *segment = NULL;
    if (!path || path[0] != '/') return -1;
    build[0] = '/';
    build[1] = '\0';
    build_len = 1u;
    while (*p == '/') ++p;
    while (*p) {
        start = p;
        len = 0u;
        segment = NULL;
        while (start[len] && start[len] != '/') ++len;
        if (len > 0u) {
            if (build_len > 1u) {
                if (build_len + 1u >= sizeof(build)) return -1;
                build[build_len++] = '/';
            }
            if (build_len + len >= sizeof(build)) return -1;
            memcpy(build + build_len, start, len);
            build_len += len;
            build[build_len] = '\0';
            segment = fs_find(build);
            if (segment && !segment->is_dir) return -1;
            if (!segment) segment = fs_alloc(build);
            if (!segment) return -1;
            segment->is_dir = 1;
            segment->is_text = 0;
        }
        p += len;
        while (*p == '/') ++p;
    }
    f = fs_find(path);
    if (f && !f->is_dir) return -1;
    if (!f) f = fs_alloc(path);
    if (!f) return -1;
    f->is_dir = 1;
    f->is_text = 0;
    return 0;
}

static const char *g_index_text;
static int g_index_rc;
static const uint8_t *g_payload_bytes;
static size_t g_payload_len;
static int g_payload_rc;
static const char *g_payload_fail_url_substr;
/* Number of leading payload fetches to fail before succeeding, used to
 * exercise the bootstrap's per-package retry. Decremented on each
 * failing fetch; 0 means "do not inject transient failures". */
static int g_payload_fail_remaining;
static int g_payload_fetch_calls;
static int g_signature_rc;
static int g_signature_calls;

/* Install-observer capture (capypkg_set_install_observer). */
static int g_obs_phase_count[CAPYPKG_INSTALL_PHASE_DONE + 1];
static int g_obs_total_calls;
static char g_obs_last_name[CAPYPKG_NAME_MAX];
static uint64_t g_obs_dl_cur_last;
static uint64_t g_obs_dl_total_last;

static int net_fetch_text(const char *url, char *buffer, size_t buffer_size,
                          size_t *out_len) {
    size_t len = 0u;
    (void)url;
    if (g_index_rc != 0 || !g_index_text) return -1;
    len = strlen(g_index_text);
    if (len + 1u > buffer_size) len = buffer_size - 1u;
    memcpy(buffer, g_index_text, len);
    buffer[len] = '\0';
    if (out_len) *out_len = len;
    return 0;
}

static int net_fetch_bytes(const char *url, uint8_t *buffer, size_t buffer_size,
                           size_t *out_len) {
    size_t len = 0u;
    ++g_payload_fetch_calls;
    if (g_payload_fail_url_substr && url &&
        strstr(url, g_payload_fail_url_substr) != NULL) {
        return -1;
    }
    if (g_payload_fail_remaining > 0) {
        --g_payload_fail_remaining;
        return -1;
    }
    if (g_payload_rc != 0 || !g_payload_bytes) return -1;
    len = g_payload_len;
    if (len > buffer_size) len = buffer_size;
    memcpy(buffer, g_payload_bytes, len);
    if (out_len) *out_len = len;
    return 0;
}

/* Progress-aware payload fetcher: streams two intermediate progress
 * samples then a final 100% before delegating to net_fetch_bytes for
 * the actual bytes. Exercises the capypkg progress fetcher + observer
 * forwarding path. */
static int net_fetch_bytes_progress(const char *url, uint8_t *buffer,
                                    size_t buffer_size, size_t *out_len,
                                    capypkg_download_progress_fn cb,
                                    void *cb_ctx) {
    int rc = net_fetch_bytes(url, buffer, buffer_size, out_len);
    if (rc == 0 && cb) {
        uint64_t total = (uint64_t)(out_len ? *out_len : 0u);
        cb(total / 2u, total, cb_ctx);
        cb(total, total, cb_ctx);
    }
    return rc;
}

static void install_observer_capture(const char *name,
                                     enum capypkg_install_phase phase,
                                     uint64_t cur, uint64_t total,
                                     void *ctx) {
    (void)ctx;
    ++g_obs_total_calls;
    if ((int)phase >= 0 && (int)phase <= CAPYPKG_INSTALL_PHASE_DONE) {
        ++g_obs_phase_count[(int)phase];
    }
    if (name) {
        strncpy(g_obs_last_name, name, sizeof(g_obs_last_name) - 1u);
        g_obs_last_name[sizeof(g_obs_last_name) - 1u] = '\0';
    }
    if (phase == CAPYPKG_INSTALL_PHASE_DOWNLOAD) {
        g_obs_dl_cur_last = cur;
        g_obs_dl_total_last = total;
    }
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
    g_payload_fail_remaining = 0;
    g_payload_fetch_calls = 0;
    g_signature_rc = 0;
    g_signature_calls = 0;
    memset(g_obs_phase_count, 0, sizeof(g_obs_phase_count));
    g_obs_total_calls = 0;
    g_obs_last_name[0] = '\0';
    g_obs_dl_cur_last = 0u;
    g_obs_dl_total_last = 0u;
    klog_reset();
    /* capypkg_reset() clears the progress fetcher + install observer; a
     * test that wants them re-binds explicitly after reset_state(). */
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
