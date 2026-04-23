#ifndef NET_HTTP_ENCODING_H
#define NET_HTTP_ENCODING_H

#include <stddef.h>
#include <stdint.h>

#define HTTP_ENCODING_OK 0
#define HTTP_ENCODING_ERR_INVALID_ARGUMENT -1
#define HTTP_ENCODING_ERR_UNSUPPORTED -2
#define HTTP_ENCODING_ERR_NO_MEMORY -3
#define HTTP_ENCODING_ERR_DATA -4
#define HTTP_ENCODING_ERR_TOO_LARGE -5

int http_encoding_requires_decode(const char *content_encoding);
int http_encoding_decode_body(const char *content_encoding,
                              const uint8_t *body, size_t body_len,
                              size_t max_output_size,
                              uint8_t **decoded_body, size_t *decoded_len);

#endif /* NET_HTTP_ENCODING_H */
