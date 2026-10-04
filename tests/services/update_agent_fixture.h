#ifndef TESTS_UPDATE_AGENT_FIXTURE_H
#define TESTS_UPDATE_AGENT_FIXTURE_H
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "security/sha256.h"
#include "services/update_payload_cache.h"

#include "services/update_agent.h"

#define UPDATE_AGENT_REPOSITORY_PATH "/system/update/repository.ini"
#define UPDATE_AGENT_CACHE_PATH "/system/update/latest.ini"
#define UPDATE_AGENT_STAGE_PATH "/system/update/staged.ini"
#define UPDATE_AGENT_STATE_PATH "/system/update/state.ini"
#define UPDATE_AGENT_IMPORT_PATH "/tmp/update-import.ini"
#define UPDATE_AGENT_FETCHED_PATH "/system/update/fetched.ini"
#define UPDATE_AGENT_PAYLOAD_CACHE_PATH "/system/update/payload.bin"
#define UPDATE_AGENT_GOOD_SHA256 \
    "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"
#define UPDATE_AGENT_ABC_SHA256 \
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
#define UPDATE_AGENT_ABC_SIZE_LINE "payload_size=3\n"
#define UPDATE_AGENT_GOOD_SIGNATURE \
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define UPDATE_AGENT_BAD_SIGNATURE \
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" \
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define UPDATE_AGENT_PAYLOAD_URL_LINE \
    "payload_url=https://github.com/test/CapyOS/releases/download/v1.0.0/kernel.bin\n"
#define UPDATE_AGENT_SIGNATURE_LINE \
    "signature_ed25519=" UPDATE_AGENT_GOOD_SIGNATURE "\n"

struct fake_file {
    const char *path;
    char text[768];
    int present;
    size_t len;
};

static struct fake_file g_files[] = {
    {UPDATE_AGENT_REPOSITORY_PATH, "", 0, 0u},
    {UPDATE_AGENT_CACHE_PATH, "", 0, 0u},
    {UPDATE_AGENT_STAGE_PATH, "", 0, 0u},
    {UPDATE_AGENT_STATE_PATH, "", 0, 0u},
    {UPDATE_AGENT_IMPORT_PATH, "", 0, 0u},
    {UPDATE_AGENT_FETCHED_PATH, "", 0, 0u},
    {UPDATE_AGENT_PAYLOAD_CACHE_PATH, "", 0, 0u},
    {UPDATE_AGENT_PAYLOAD_CACHE_PATH ".part0", "", 0, 0u},
    {UPDATE_AGENT_PAYLOAD_CACHE_PATH ".part1", "", 0, 0u},
    {UPDATE_AGENT_PAYLOAD_CACHE_PATH ".part2", "", 0, 0u},
    {UPDATE_AGENT_PAYLOAD_CACHE_PATH ".part3", "", 0, 0u},
};

static const char *g_fetch_text;
static uint8_t *g_large_files[sizeof(g_files) / sizeof(g_files[0])];
static int g_fetch_rc;
static char g_last_fetch_url[192];
static const uint8_t *g_payload_bytes;
static size_t g_payload_len;
static size_t g_last_payload_buffer_size;
static int g_payload_rc;
static int g_payload_failures_remaining;
static int g_payload_fetch_calls;
static int g_corrupt_payload_write;
static char g_last_payload_url[192];

static int expect_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "[update_agent] %s\n", msg);
        return 1;
    }
    return 0;
}

static struct fake_file *find_file(const char *path) {
    size_t i = 0;
    if (!path) {
        return NULL;
    }
    for (i = 0; i < sizeof(g_files) / sizeof(g_files[0]); ++i) {
        if (strcmp(g_files[i].path, path) == 0) {
            return &g_files[i];
        }
    }
    return NULL;
}

static void set_file_text(const char *path, const char *text) {
    struct fake_file *file = find_file(path);
    if (!file) {
        return;
    }
    free(g_large_files[file - g_files]);
    g_large_files[file - g_files] = NULL;
    file->present = text ? 1 : 0;
    file->text[0] = '\0';
    file->len = 0u;
    if (text) {
        strncpy(file->text, text, sizeof(file->text) - 1u);
        file->text[sizeof(file->text) - 1u] = '\0';
        file->len = strlen(file->text);
    }
}

static void reset_files(void) {
    size_t i = 0;
    for (i = 0; i < sizeof(g_files) / sizeof(g_files[0]); ++i) {
        free(g_large_files[i]);
        g_large_files[i] = NULL;
        g_files[i].present = 0;
        g_files[i].text[0] = '\0';
        g_files[i].len = 0u;
    }
    g_fetch_text = NULL;
    g_fetch_rc = -1;
    g_last_fetch_url[0] = '\0';
    g_payload_bytes = NULL;
    g_payload_len = 0u;
    g_last_payload_buffer_size = 0u;
    g_payload_rc = -1;
    g_payload_failures_remaining = 0;
    g_payload_fetch_calls = 0;
    g_corrupt_payload_write = 0;
    g_last_payload_url[0] = '\0';
}

static int stub_read_file(const char *path, char *buffer, size_t buffer_size,
                          size_t *out_len) {
    struct fake_file *file = find_file(path);
    size_t len = 0u;
    size_t i = 0u;

    if (!file || !file->present || !buffer || buffer_size == 0u) {
        return -1;
    }
    len = file->len;
    if (len + 1u > buffer_size) {
        len = buffer_size - 1u;
    }
    for (i = 0u; i < len; ++i) {
        buffer[i] = file->text[i];
    }
    buffer[len] = '\0';
    if (out_len) {
        *out_len = len;
    }
    return 0;
}

static int stub_write_file(const char *path, const char *text) {
    if (!path || !text) {
        return -1;
    }
    set_file_text(path, text);
    return 0;
}

static int stub_read_bytes(const char *path, uint8_t *buffer,
                           size_t buffer_size, size_t *out_len) {
    struct fake_file *file = find_file(path);
    size_t i = 0u;
    if (!file || !file->present || !buffer || file->len > buffer_size) {
        return -1;
    }
    while (i < file->len) {
        buffer[i] = g_large_files[file - g_files] ?
                    g_large_files[file - g_files][i] : (uint8_t)file->text[i];
        ++i;
    }
    if (out_len) {
        *out_len = file->len;
    }
    return 0;
}

static int stub_write_bytes(const char *path, const uint8_t *data, size_t len) {
    struct fake_file *file = find_file(path);
    size_t i = 0u;
    if (!file || (!data && len > 0u) || len > UPDATE_CACHE_SINGLE_BYTES) {
        return -1;
    }
    free(g_large_files[file - g_files]);
    g_large_files[file - g_files] = NULL;
    if (len >= sizeof(file->text)) {
        uint8_t *large = malloc(len);
        if (!large) return -1;
        memcpy(large, data, len);
        if (g_corrupt_payload_write) large[0] ^= 1;
        g_large_files[file - g_files] = large;
        file->present = 1;
        file->len = len;
        return 0;
    }
    file->present = 1;
    while (i < len) {
        file->text[i] = (char)data[i];
        ++i;
    }
    if (g_corrupt_payload_write && len > 0u) {
        file->text[0] ^= 1;
    }
    file->text[len] = '\0';
    file->len = len;
    return 0;
}

static int stub_remove_file(const char *path) {
    struct fake_file *file = find_file(path);
    if (!file) {
        return -1;
    }
    free(g_large_files[file - g_files]);
    g_large_files[file - g_files] = NULL;
    file->present = 0;
    file->text[0] = '\0';
    file->len = 0u;
    return 0;
}

static int stub_manifest_verify(const char *signed_text, size_t signed_len,
                                const char *signature_hex) {
    return signed_text && signed_len > 0u && signature_hex &&
           strstr(signed_text, "signature_ed25519=") == NULL &&
           strcmp(signature_hex, UPDATE_AGENT_GOOD_SIGNATURE) == 0;
}

static int stub_fetch_manifest(const char *url, char *buffer, size_t buffer_size,
                               size_t *out_len) {
    size_t len = 0u;
    if (url) {
        strncpy(g_last_fetch_url, url, sizeof(g_last_fetch_url) - 1u);
        g_last_fetch_url[sizeof(g_last_fetch_url) - 1u] = '\0';
    }
    if (g_fetch_rc != 0 || !g_fetch_text || !buffer || buffer_size == 0u) {
        return g_fetch_rc ? g_fetch_rc : -1;
    }
    len = strlen(g_fetch_text);
    if (len + 1u > buffer_size) {
        return -2;
    }
    strncpy(buffer, g_fetch_text, buffer_size - 1u);
    buffer[buffer_size - 1u] = '\0';
    if (out_len) {
        *out_len = len;
    }
    return 0;
}

static int stub_fetch_payload(const char *url, uint8_t *buffer,
                              size_t buffer_size, size_t *out_len) {
    size_t i = 0u;
    g_payload_fetch_calls++;
    g_last_payload_buffer_size = buffer_size;
    if (url) {
        strncpy(g_last_payload_url, url, sizeof(g_last_payload_url) - 1u);
        g_last_payload_url[sizeof(g_last_payload_url) - 1u] = '\0';
    }
    if (g_payload_failures_remaining > 0) {
        g_payload_failures_remaining--;
        return -1;
    }
    if (g_payload_rc != 0 || !g_payload_bytes || !buffer ||
        g_payload_len > buffer_size) {
        return g_payload_rc ? g_payload_rc : -1;
    }
    while (i < g_payload_len) {
        buffer[i] = g_payload_bytes[i];
        ++i;
    }
    if (out_len) {
        *out_len = g_payload_len;
    }
    return 0;
}


#endif
