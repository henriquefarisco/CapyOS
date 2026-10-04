#ifndef CORE_RUNTIME_VERSION_H
#define CORE_RUNTIME_VERSION_H

#include "core/version.h"

/* The bridge is an OTA-only, reduced asset build, never an installer release. */
#if defined(CAPYOS_MIGRATION_BRIDGE)
#define CAPYOS_RUNTIME_VERSION_EXTENDED "0.11.2"
#define CAPYOS_RUNTIME_VERSION_FULL "0.11.2+20261004"
#else
#define CAPYOS_RUNTIME_VERSION_EXTENDED CAPYOS_VERSION_EXTENDED
#define CAPYOS_RUNTIME_VERSION_FULL CAPYOS_VERSION_FULL
#endif

#endif
