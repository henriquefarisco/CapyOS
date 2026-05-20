#ifndef KERNEL_MODULE_GATE_H
#define KERNEL_MODULE_GATE_H

/*
 * kernel/module_gate.h — alpha.241
 *
 * Boot-time gate that decides whether the kernel may activate a
 * module-owned subsystem. The gate is the only contract between the
 * core (which always ships in the kernel ELF when PROFILE=full) and
 * the modules published by sibling repos (CapyUI, CapyBrowser, ...).
 *
 * Today the gate checks the staged-module marker that the capypkg
 * adapter writes to /var/capypkg/<module-name>/installed when a
 * capypkg_install completes successfully. A future loader will use
 * the same gate to discover entry points after staging; until then
 * the gate just prevents the core from starting a subsystem the
 * user did not opt into.
 *
 * The core build profile selector (CAPYOS_PROFILE_CORE_ONLY) is the
 * outermost short-circuit: when defined, the gate refuses every
 * module unconditionally because the kernel ELF was linked without
 * the module-owned code in the first place.
 *
 * Stable canonical names used by the gate (do NOT rename without a
 * coordinated bump in CapyUI/Makefile, the in-tree adapter docs and
 * docs/reference/integration/compatibility-matrix.md):
 *
 *   - org.capyos.ui.widget-core       (CapyUI widget primitives)
 *   - org.capyos.ui.desktop-session   (CapyUI desktop + window mgr + apps)
 *
 * The functions are deliberately scalar booleans (1/0) with NO
 * out-of-band error reporting because callers must fail closed
 * regardless of why the check refused.
 */

/* Return 1 when the CapyUI desktop-session module is installed AND
 * the kernel was built in a profile that contains the desktop-side
 * code. Otherwise return 0. */
int kernel_module_desktop_session_available(void);

/* Return 1 when the CapyUI widget-core module is installed. The
 * desktop-session module depends on this; checking it separately
 * is mostly useful for diagnostics. */
int kernel_module_widget_core_available(void);

/* Return 1 when the named module is staged (its `installed` marker
 * file exists under /var/capypkg/<name>/installed). Use the public
 * canonical-name constants above instead of stringly-typed lookups
 * when possible. Never logs; never opens the network. */
int kernel_module_is_installed(const char *canonical_name);

#endif /* KERNEL_MODULE_GATE_H */
