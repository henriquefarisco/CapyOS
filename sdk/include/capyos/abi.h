#ifndef CAPYOS_SDK_ABI_H
#define CAPYOS_SDK_ABI_H

#include <stdint.h>

#define CAPYOS_SDK_ABI_NAME "capyos-base"
#define CAPYOS_SDK_ABI_VERSION 3u
#define CAPYOS_SDK_ABI_TOKEN "capyos-base-v3"

struct capyos_sdk_abi_info {
  uint32_t struct_size;
  uint32_t abi_version;
  const char *abi_name;
  const char *abi_token;
};

#endif
