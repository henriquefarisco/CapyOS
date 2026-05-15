#ifndef SECURITY_TLS_HOSTNAME_POLICY_H
#define SECURITY_TLS_HOSTNAME_POLICY_H

#include <stddef.h>

static inline int tls_hostname_policy_char_safe(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '-';
}

static inline int tls_hostname_policy_valid(const char *hostname) {
  size_t total_len = 0;
  size_t label_len = 0;
  char first = '\0';
  char prev = '\0';
  if (!hostname || !hostname[0]) return 0;
  while (*hostname) {
    unsigned char c = (unsigned char)*hostname;
    total_len++;
    if (total_len > 253u) return 0;
    if (c <= 0x20u || c == 0x7fu ||
        !tls_hostname_policy_char_safe((char)c)) return 0;
    if (c == '.') {
      if (label_len == 0 || first == '-' || prev == '-') return 0;
      label_len = 0;
      first = '\0';
      prev = (char)c;
      hostname++;
      continue;
    }
    if (label_len == 0) first = (char)c;
    label_len++;
    if (label_len > 63u) return 0;
    prev = (char)c;
    hostname++;
  }
  return label_len != 0 && first != '-' && prev != '-';
}

#endif
