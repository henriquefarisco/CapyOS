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
   - Theme
   - Splash on/off
   - Admin user + password (with confirmation)
   - Module selection (NEW):
       BASIC  - core + capysh only
       FULL   - desktop, apps, recommended modules
       CUSTOM - operator picks the module list

4. If profile != BASIC:
   - Writes /system/install/profile.ini via install_profile_format.
   - Calls capypkg_bootstrap_run_with_progress(force=1) so the
     progress callback prints "[i/N] org.capyos.ui.desktop-session..."
     on the fbcon while bytes flow in.
   - On install_count > 0, returns 1 to program.c which logs
     "Rebooting to activate modules..." and calls acpi_reboot().

5. After reboot:
   - The desktop activation gate succeeds because
     /var/capypkg/org.capyos.ui.desktop-session/installed now
     exists, so desktop_runtime_start() boots straight into the GUI.

6. Subsequent boots:
   - /system/first-run.done is present → wizard skipped.
   - /system/install/bootstrap.done is present → capypkg kernel
     poll returns idle.
```

If the profile selection picks BASIC, step 4 only writes
`profile.ini` with `kind=basic` and skips the bootstrap call; no
reboot happens and the system continues into capysh.

## 3. Surfaces involved

| Layer                      | TU                                                         |
|----------------------------|------------------------------------------------------------|
| Installer UEFI             | `src/boot/uefi_loader/installer_run.c`                     |
| Boot config                | `src/boot/uefi_loader/efi_main.c`                          |
| Kernel storage bring-up    | `src/arch/x86_64/kernel_shell_runtime.c`                   |
| Wizard top-level           | `src/config/first_boot/program.c`                          |
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

## 5. Failure modes and recovery

| Symptom                                     | Cause                                                | Recovery                                                                              |
|---------------------------------------------|------------------------------------------------------|----------------------------------------------------------------------------------------|
| Wizard never appears                        | `BOOT_CONFIG_FLAG_HAS_SETUP_DATA` set by legacy ISO  | Reinstall with an alpha.241+ ISO, or clear `/system/first-run.done` and run `capy wizard` |
| Modules step shows "adapter not yet ready"  | capypkg bind not done yet                            | Kernel auto-bootstrap retries every 60s; or run `capy wizard --modules` after boot     |
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
