# First-boot wizard (alpha.241)

**Status:** authoritative since 2026-05-19 (alpha.241).
**Audience:** anyone who modifies the boot path, installer UEFI,
first-boot configuration code, or the capypkg adapter.

## 1. Why a single wizard

Before alpha.241 the installer UEFI loader collected language,
keyboard, hostname, theme, splash, admin user and password and
shipped them to the kernel via `BOOT_CONFIG_FLAG_HAS_SETUP_DATA`.
The kernel then ran a "silent provisioning" path that wrote the
admin user, the config file and the `/system/first-run.done` marker
before the desktop ever started — meaning the user never saw any
wizard.

This had three problems:

1. The installer ran the wizard, but the **installed** system had
   no UI to reconfigure those choices later without re-flashing
   the ISO.
2. Module installation was never asked for, so every install ended
   up with the same bundled set; remote modules were dead code.
3. Two implementations of the same prompts had to be kept in sync
   (`installer_run.c` and `program.c::first_boot_setup_interactive`).

alpha.241 retires the installer wizard. The kernel's
`first_boot_setup_interactive` is now the **single** source of
truth and ALSO asks about modules to install.

## 2. End-to-end flow

```
1. ISO boots installer UEFI loader.
   - Confirms install (Y/n).
   - Generates recovery key, shows it once.
   - Wipes disk, writes GPT, FAT32 ESP, BOOT partition, kernel.
   - boot_cfg.flags = BOOT_CONFIG_FLAG_HAS_VOLUME_KEY only.
   - Reboots.

2. Kernel boots fresh disk for the first time.
   - kernel_shell_runtime: mounts encrypted volume (no silent
     provisioning anymore).
   - kernel_main: detects first-boot, runs system_login().
   - system_login() sees no admin and dispatches
     system_run_first_boot_setup() → first_boot_setup_interactive().

3. Wizard TUI on the framebuffer console (locale-aware):
   - Keyboard layout selection
   - Hostname
   - Splash on/off          (Theme is no longer asked here; see §2.1)
   - Admin user + password (with confirmation)
   - Module selection (NEW):
       BASIC  - core + capysh only
       FULL   - desktop, apps, recommended modules
       CUSTOM - operator picks the module list

4. If profile != BASIC:
   - Writes /system/install/profile.ini via install_profile_format.
   - Registers a capypkg install observer and calls
     capypkg_bootstrap_run_with_progress(force=1); the wizard renders a
     live status bar (overall + per-package download/verify/install)
     instead of a scrolling log. See §2.1.
   - On install_count > 0, returns 1 to program.c which logs
     "Rebooting to activate modules..." and calls acpi_reboot().

5. After reboot:
   - The desktop activation gate succeeds because
     /var/capypkg/org.capyos.ui.desktop-session/installed now
     exists, so desktop_runtime_start() boots straight into the GUI.

6. Subsequent boots:
   - /system/first-run.done is present → wizard skipped.
   - /system/install/bootstrap.done is present as a file → capypkg
     kernel poll returns idle.
```

If the profile selection picks BASIC, step 4 only writes
`profile.ini` with `kind=basic` and skips the bootstrap call; no
reboot happens and the system continues into capysh.

## 2.1 Theme, install progress, ordering and retry (alpha.256)

**Theme removed from the wizard.** The visual theme only has an effect
once the optional CapyUI desktop-session module is installed, and that
install is decided in the module-selection step. Asking for a theme
earlier would either be a no-op (BASIC, no desktop) or force the choice
before the desktop is even selected. The wizard therefore writes the
canonical `capyos` theme to `/system/config.ini`; the desktop module
(and the `config-theme` CLI command) own the user-facing theme switch
after install.

The module-install sweep was reworked so the wizard is honest about
what it is doing and resilient to flaky networks:

- **Live status bar.** `modules.c` owns a `struct modules_ui_state`
  rendered by `modules_progress.c`. Two callbacks feed it: the coarse
  `capypkg_bootstrap` event callback (repo/index/package
  begin/ok/fail/retry/sweep) and a fine `capypkg` install observer that
  reports the current package's phase (RESOLVE → DOWNLOAD → VERIFY →
  STAGE → DONE) and byte-level download progress. The download bytes
  come from a new `http_download_progress()` plumbed through an optional
  `capypkg` progress-aware bytes fetcher; when that fetcher is not bound
  (e.g. host tests) the install still works and falls back to the plain
  fetcher. Redraws are throttled (~30 ms) so a fast download does not
  flood the framebuffer.
- **Dependency-ordered waves.** `capypkg_bootstrap` no longer installs
  in raw catalog order. It builds a plan: select the target set (FULL =
  all available; CUSTOM = the picked list expanded with transitive
  in-catalog dependencies), then group targets into dependency *waves*.
  A package lands in the earliest wave where all of its in-target
  dependencies are already placed (or already installed). Packages in a
  wave have no dependency relationship, so within a wave order is
  irrelevant — they are independent. `capypkg_install` still resolves
  dependencies recursively as a backstop.
- **Per-package retry.** Each package is installed with an individual
  in-place retry (up to 3 attempts) for transient errors (fetch /
  storage / digest); one flaky download no longer restarts the whole
  sweep. The `CAPYPKG_BOOTSTRAP_EVENT_PACKAGE_RETRY` event surfaces this
  to the UI. The wizard's outer sweep-level retry (2/4/8 s backoff)
  still covers longer outages (DHCP/DNS/TLS warm-up).
- **Parallel-ready seam.** Within a wave the packages are independent
  and could be installed concurrently. They run sequentially today
  because the network/TLS/VFS/`capypkg` layers are single-threaded and
  the wizard runs before the preemptive scheduler is enabled
  (`CAPYOS_PREEMPTIVE_SCHEDULER` is off by default, so worker-pool
  threads would not be dispatched and `worker_pool_drain` would spin).
  The wave structure is the single dispatch seam a future change can
  switch to a kernel worker pool once those layers are reentrant.

## 3. Surfaces involved

| Layer                      | TU                                                         |
|----------------------------|------------------------------------------------------------|
| Installer UEFI             | `src/boot/uefi_loader/installer_run.c`                     |
| Boot config                | `src/boot/uefi_loader/efi_main.c`                          |
| Kernel storage bring-up    | `src/arch/x86_64/kernel_shell_runtime.c`                   |
| Wizard top-level           | `src/config/first_boot/program.c`                          |
| Wizard install status bar  | `src/config/first_boot/modules_progress.c`                |
| capypkg download progress  | `src/net/services/http/redirect_download.c` (`http_download_progress`) |
| Wizard module selection    | `src/config/first_boot/modules.c`                          |
| Wizard TUI helpers         | `src/config/system_setup_wizard.c`                         |
| Module profile parser      | `src/services/install_profile.c`                           |
| capypkg bootstrap          | `src/services/capypkg_bootstrap.c`                         |
| Activation gate            | `src/kernel/module_gate.c`                                 |
| Unified shell command      | `src/shell/commands/system_control/capy_command.c`         |

## 4. Re-running the wizard later

The `capy wizard` shell command unlinks `/system/first-run.done`
and `/system/install/bootstrap.done` and re-runs
`system_run_first_boot_setup()`.

`capy wizard --modules` skips the personal data prompts and only
re-runs the module selection step (it clears
`/system/install/bootstrap.done` but leaves the user/config alone).
If the module bootstrap reports an incomplete retryable state, the
command returns an error and the kernel auto-bootstrap poll continues
retrying from the same `profile.ini`.

## 5. Failure modes and recovery

| Symptom                                     | Cause                                                | Recovery                                                                              |
|---------------------------------------------|------------------------------------------------------|----------------------------------------------------------------------------------------|
| Wizard never appears                        | `BOOT_CONFIG_FLAG_HAS_SETUP_DATA` set by legacy ISO  | Reinstall with an alpha.241+ ISO, or clear `/system/first-run.done` and run `capy wizard` |
| Modules step shows "adapter not yet ready"  | storage/runtime bind failed before the wizard could bind capypkg | Kernel auto-bootstrap retries every 60s; or run `capy wizard --modules` after boot     |
| Modules step says "network unavailable"     | DHCP not yet ready, or repo URL unreachable          | Same; bootstrap retries; or fix profile.ini and run `pkg-bootstrap --force`           |
| `capy install` says "Desktop not installed" | Activation gate; module never staged                 | `capy install org.capyos.ui.desktop-session` or `capy wizard --modules`                |
| Desktop never starts after reboot           | Module installed but `installed` marker missing      | Inspect `/var/capypkg/org.capyos.ui.desktop-session/`, re-run `pkg-install <name>`     |

## 6. References

- [`modular-build-profiles.md`](modular-build-profiles.md)
- [`capypkg-adapter.md`](capypkg-adapter.md)
- [`../reference/integration/capypkg-publisher-manifest-format.md`](../reference/integration/capypkg-publisher-manifest-format.md)
- [`../operations/manual-module-deploy-runbook.md`](../operations/manual-module-deploy-runbook.md)
- `include/services/install_profile.h`
- `include/services/capypkg_bootstrap.h`
- `include/kernel/module_gate.h`
