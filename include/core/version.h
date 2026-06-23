#ifndef CORE_VERSION_H
#define CORE_VERSION_H

/* Version macros exported to the runtime and host tooling. */
#define CAPYOS_VERSION_MAJOR        0
#define CAPYOS_VERSION_MINOR        8
#define CAPYOS_VERSION_PATCH        0

#define CAPYOS_VERSION_CHANNEL      "alpha"
#define CAPYOS_VERSION_PRERELEASE   "alpha.284"
#define CAPYOS_VERSION_EXTENDED     "0.8.0-alpha.284"
#define CAPYOS_VERSION_FULL         "0.8.0-alpha.284+20260617"
#define CAPYOS_FEATURE_HYPERV_RUNTIME "hvrt-20260328a"
#define CAPYOS_FEATURE_NETWORK_DIAG   "netdiag-20260328a"
/* Etapa 2 do roteiro de maturação do browser (2026-05-03):
 * pipe 64 KiB + log forward + kill imediato + janela 480x384. */
#define CAPYOS_FEATURE_BROWSER_RUNTIME "browser-20260503a"

#define CAPYOS_VERSION_ALPHA        "0.8.0-alpha.284"
#define CAPYOS_VERSION_BETA         "0.6.2-beta.3"
#define CAPYOS_VERSION_STABLE       "0.8.1"

#endif
