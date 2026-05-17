/*
 * src/auth/user_helpers.c
 *
 * Internal helpers shared inside the `auth` module. See
 * `src/auth/internal/user_helpers.h` for the rationale; the
 * implementations here used to live as `static` symbols inside
 * `src/auth/user.c`. Exposing them as extern (with the `auth_` prefix
 * to avoid collisions with any future shared helpers in `src/util/`)
 * was necessary when `user.c` was split into separate translation
 * units for user-record, userdb-io and userdb-auth.
 */
#include "auth/internal/user_helpers.h"

#include "security/csprng.h"

size_t auth_cstring_length(const char *s) {
    size_t len = 0;
    if (!s) {
        return 0;
    }
    while (s[len]) {
        ++len;
    }
    return len;
}

/* Defensive zeroing helper used for credential buffers (raw passwords,
 * salts, derived hashes, parsed user records, /etc/passwd snapshots).
 * The pointer is marked volatile so the compiler cannot eliminate the
 * stores as dead even when the destination buffer is freed or leaves
 * scope immediately after — that dead-store elimination is the classic
 * way "secure wipe" code gets silently optimised away in C. */
void auth_memory_zero(void *dst, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    while (len--) {
        *p++ = 0;
    }
}

void auth_cstring_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    size_t i = 0;
    if (src) {
        while (src[i] && i < dst_size - 1) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

void auth_cstring_copy_n(char *dst, size_t dst_size, const char *src,
                         size_t src_len) {
    if (!dst || dst_size == 0) {
        return;
    }
    size_t to_copy = 0;
    if (src && src_len > 0) {
        to_copy = (src_len < dst_size - 1) ? src_len : (dst_size - 1);
        for (size_t i = 0; i < to_copy; ++i) {
            dst[i] = src[i];
        }
    }
    dst[to_copy] = '\0';
}

int auth_strings_equal(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }
    size_t ia = 0;
    while (a[ia] && b[ia]) {
        if (a[ia] != b[ia]) {
            return 0;
        }
        ++ia;
    }
    return a[ia] == b[ia];
}

void auth_bytes_to_hex(const uint8_t *src, size_t len, char *dst) {
    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        uint8_t v = src[i];
        dst[i * 2] = hex_digits[(v >> 4) & 0x0F];
        dst[i * 2 + 1] = hex_digits[v & 0x0F];
    }
    dst[len * 2] = '\0';
}

int auth_hex_to_bytes(const char *hex, size_t hex_len, uint8_t *dst,
                      size_t dst_len) {
    if (!hex || !dst || hex_len != dst_len * 2) {
        return -1;
    }
    for (size_t i = 0; i < dst_len; ++i) {
        char h = hex[i * 2];
        char l = hex[i * 2 + 1];
        uint8_t hv;
        uint8_t lv;
        if (h >= '0' && h <= '9') hv = h - '0';
        else if (h >= 'a' && h <= 'f') hv = (uint8_t)(10 + h - 'a');
        else if (h >= 'A' && h <= 'F') hv = (uint8_t)(10 + h - 'A');
        else return -1;

        if (l >= '0' && l <= '9') lv = l - '0';
        else if (l >= 'a' && l <= 'f') lv = (uint8_t)(10 + l - 'a');
        else if (l >= 'A' && l <= 'F') lv = (uint8_t)(10 + l - 'A');
        else return -1;

        dst[i] = (hv << 4) | lv;
    }
    return 0;
}

uint32_t auth_parse_u32(const char *str, size_t len) {
    uint32_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (c < '0' || c > '9') {
            return value;
        }
        value = value * 10u + (uint32_t)(c - '0');
    }
    return value;
}

void auth_u32_to_string(uint32_t value, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) {
        return;
    }
    char tmp[10];
    size_t len = 0;
    if (value == 0) {
        if (buf_len >= 2) {
            buf[0] = '0';
            buf[1] = '\0';
        } else {
            buf[0] = '\0';
        }
        return;
    }
    while (value && len < sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }
    size_t pos = 0;
    while (len && pos + 1 < buf_len) {
        buf[pos++] = tmp[--len];
    }
    buf[pos] = '\0';
}

void auth_generate_salt(uint8_t *salt, size_t len) {
    csprng_get_bytes(salt, len);
}

int auth_append_piece(char *dst, size_t cap, size_t *idx, const char *src) {
    if (!dst || !idx || !src || *idx >= cap) {
        return -1;
    }
    size_t plen = auth_cstring_length(src);
    if (*idx + plen >= cap) {
        return -1;
    }
    for (size_t i = 0; i < plen; ++i) {
        dst[(*idx)++] = src[i];
    }
    return 0;
}
