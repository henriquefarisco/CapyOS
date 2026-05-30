#ifndef SERVICES_CAPYPKG_LOCAL_BUNDLE_H
#define SERVICES_CAPYPKG_LOCAL_BUNDLE_H

#include <stddef.h>
#include <stdint.h>

#define CAPYPKG_LOCAL_BUNDLE_BASE_URL "https://local.capyos.invalid/capypkg/"
#define CAPYPKG_LOCAL_BUNDLE_INDEX_URL \
    "https://local.capyos.invalid/capypkg/modules-index.txt"

struct capypkg_local_bundle_blob {
    const char *url;
    const uint8_t *data;
    size_t len;
};

int capypkg_local_bundle_available(void);
int capypkg_local_bundle_fetch_text(const char *url, char *buffer,
                                    size_t buffer_size, size_t *out_len);
int capypkg_local_bundle_fetch_bytes(const char *url, uint8_t *buffer,
                                     size_t buffer_size, size_t *out_len);

#endif
