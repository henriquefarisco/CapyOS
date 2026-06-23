#ifndef APPS_APPS_SMOKE_H
#define APPS_APPS_SMOKE_H

/*
 * Etapa 6 / Slice 6.6 — apps-basic-roundtrip smoke contract.
 *
 * CapyOS-owned declarations implemented by CapyUI (src/apps/apps_smoke.c),
 * compiled into the kernel as part of the desktop-session app set. The CapyOS
 * orchestrator (under CAPYOS_APPS_ROUNDTRIP_SMOKE, src/kernel/user_init.c) calls
 * these to run each basic app's headless primary-function roundtrip and count
 * clean passes, then emits `[smoke] apps-basic-roundtrip ready` on COM1.
 *
 * The apps are in-kernel functions, not ring-3 processes, so there is no
 * process exit code; each per-app roundtrip exercises primary logic only (no
 * GUI/compositor) and returns 0 on success.
 *
 * Versioned surface of the `capy-ui-desktop-session` ABI (additive).
 */

/* Number of apps in the roundtrip set. The CapyOS smoke gate must set
 * APPS_ROUNDTRIP_SMOKE_REQUIRED_APPS to this value. */
unsigned apps_smoke_roundtrip_total(void);

/* Run app[index]'s headless smoke roundtrip. Returns 0 on success, non-zero on
 * failure (including an out-of-range index). No GUI is initialized. */
int apps_smoke_roundtrip_run(unsigned index);

#endif /* APPS_APPS_SMOKE_H */
