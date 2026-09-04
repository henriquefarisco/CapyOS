#include "capyos/package.h"

int capyos_package_entry(const struct capyos_package_context *ctx) {
  if (!ctx || ctx->struct_size < sizeof(*ctx) ||
      ctx->core_abi_version != CAPYOS_SDK_ABI_VERSION || !ctx->log) {
    return -1;
  }
  ctx->log("hello from a capyos-base-v3 package");
  return 0;
}
