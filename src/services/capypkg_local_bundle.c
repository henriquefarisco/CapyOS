#include "services/capypkg_local_bundle.h"

#ifdef CAPYOS_LOCAL_CAPYPKG_BUNDLE
#include "capypkg_local_bundle_data.h"
#define CAPYPKG_LOCAL_BUNDLE_ACTIVE 1
#elif defined(UNIT_TEST)
static const uint8_t capypkg_local_test_index[] =
    "name=org.capyos.test.local\n"
    "version=0.0.1\n"
    "summary=local fixture\n"
    "payload_url=https://local.capyos.invalid/capypkg/org.capyos.test.local-0.0.1.bin\n"
    "payload_sha256=fixture\n"
    "payload_size=3\n"
    "---\n";
static const uint8_t capypkg_local_test_payload[] = {0x43u, 0x41u, 0x50u};
const struct capypkg_local_bundle_blob capypkg_local_bundle_blobs[] = {
    {CAPYPKG_LOCAL_BUNDLE_INDEX_URL, capypkg_local_test_index,
     sizeof(capypkg_local_test_index) - 1u},
    {"https://local.capyos.invalid/capypkg/org.capyos.test.local-0.0.1.bin",
     capypkg_local_test_payload, sizeof(capypkg_local_test_payload)},
};
const size_t capypkg_local_bundle_blob_count =
    sizeof(capypkg_local_bundle_blobs) / sizeof(capypkg_local_bundle_blobs[0]);
#define CAPYPKG_LOCAL_BUNDLE_ACTIVE 1
#endif

#ifdef CAPYPKG_LOCAL_BUNDLE_ACTIVE
static int local_bundle_equal(const char *a, const char *b) {
    size_t i = 0u;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static const struct capypkg_local_bundle_blob *local_bundle_find(const char *url) {
    if (!url) {
        return NULL;
    }
    for (size_t i = 0u; i < capypkg_local_bundle_blob_count; ++i) {
        if (local_bundle_equal(url, capypkg_local_bundle_blobs[i].url)) {
            return &capypkg_local_bundle_blobs[i];
        }
    }
    return NULL;
}
#endif

int capypkg_local_bundle_available(void) {
#ifdef CAPYPKG_LOCAL_BUNDLE_ACTIVE
    return capypkg_local_bundle_blob_count > 0u ? 1 : 0;
#else
    return 0;
#endif
}

int capypkg_local_bundle_fetch_text(const char *url, char *buffer,
                                    size_t buffer_size, size_t *out_len) {
#ifdef CAPYPKG_LOCAL_BUNDLE_ACTIVE
    const struct capypkg_local_bundle_blob *blob = local_bundle_find(url);
    if (!blob || !buffer || buffer_size == 0u) {
        return -1;
    }
    if (blob->len + 1u > buffer_size || blob->len + 1u < blob->len) {
        return -1;
    }
    for (size_t i = 0u; i < blob->len; ++i) {
        unsigned char c = blob->data[i];
        if (c == 0u) {
            return -1;
        }
        buffer[i] = (char)c;
    }
    buffer[blob->len] = '\0';
    if (out_len) {
        *out_len = blob->len;
    }
    return 0;
#else
    (void)url;
    (void)buffer;
    (void)buffer_size;
    (void)out_len;
    return -1;
#endif
}

int capypkg_local_bundle_fetch_bytes(const char *url, uint8_t *buffer,
                                     size_t buffer_size, size_t *out_len) {
#ifdef CAPYPKG_LOCAL_BUNDLE_ACTIVE
    const struct capypkg_local_bundle_blob *blob = local_bundle_find(url);
    if (!blob || !buffer) {
        return -1;
    }
    if (blob->len > buffer_size) {
        return -1;
    }
    for (size_t i = 0u; i < blob->len; ++i) {
        buffer[i] = blob->data[i];
    }
    if (out_len) {
        *out_len = blob->len;
    }
    return 0;
#else
    (void)url;
    (void)buffer;
    (void)buffer_size;
    (void)out_len;
    return -1;
#endif
}
