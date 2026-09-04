#ifndef CAPYOS_SDK_PACKAGE_H
#define CAPYOS_SDK_PACKAGE_H

#include <stddef.h>
#include <stdint.h>
#include "capyos/abi.h"

#define CAPYOS_PACKAGE_ENTRY_ABI 1u

struct capyos_package_context {
  uint32_t struct_size;
  uint32_t core_abi_version;
  void (*log)(const char *message);
  void *reserved[5];
};

typedef int (*capyos_package_entry_fn)(const struct capyos_package_context *ctx);

#endif
