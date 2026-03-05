# CapyOS Architecture Overview

This document describes the architecture currently implemented in the
repository. CapyOS now follows a single active execution track focused on
`UEFI/GPT + x86_64`.

## 1. Execution track

### Active track: UEFI/GPT + x86_64
- Boot chain: `BOOTX64.EFI` (`src/boot/uefi_loader.c`) -> kernel ELF64.
- Loader resolves manifest from the raw `BOOT` GPT partition and falls back to
  ESP files when needed.
- Runtime uses the encrypted `DATA` partition for login, filesystem and CLI
  persistence.

### Legacy track status: BIOS/MBR + x86 32-bit
- Legacy boot/runtime flow was removed from the supported build and release
  pipeline.
- Residual code under legacy directories is migration debt and should be
  treated as removal backlog, not as a supported runtime.

## 2. Boot architecture

### 2.1 UEFI path
1. Firmware starts `BOOTX64.EFI`.
2. Loader resolves manifest/kernel from the GPT `BOOT` partition or ESP
   fallback.
3. Loader builds a handoff (`include/boot/handoff.h`) with framebuffer,
   memory map, ACPI RSDP and runtime disk metadata.
4. Control transfers to `kernel_main64`
   (`src/arch/x86_64/kernel_main.c`).

### 2.2 Current boot constraints
- The x64 path is validated for `HDD/SSD provisioned by GPT script -> UEFI boot
  from disk`.
- The loader still operates in a hybrid firmware-dependent mode for some input
  scenarios and does not fully finalize `ExitBootServices`.
- The ISO installer path exists, but it still lacks a dedicated end-to-end
  smoke that covers install -> reboot from HDD -> login -> persistence.

## 3. Disk layout

### 3.1 UEFI provisioning layout (GPT)
- ESP (FAT32): `EFI/BOOT/BOOTX64.EFI`, fallback config and loader assets.
- BOOT (raw): manifest at partition start + raw kernel payload sectors.
- DATA: encrypted CAPYFS volume used by the runtime.

### 3.2 Provisioning and inspection
- Provisioning: `tools/scripts/provision_gpt.py`
- Audit: `tools/scripts/inspect_disk.py`
- Supported validation command:
  - `make inspect-disk IMG=build/disk-gpt.img`

## 4. Storage and filesystem stack

- `fs/block`: generic block-device interface.
- `fs/storage/*`: wrappers (offset/chunk), partition parser, base block device.
- `fs/cache/buffer_cache.c`: buffered block cache and sync.
- `fs/capyfs/capyfs.c`: native CAPYFS implementation.
- `fs/vfs/vfs.c`: path resolution, dentries, metadata and permission checks.

CAPYFS uses:
- superblock + inode bitmap + block bitmap
- inode with direct pointers + one indirect pointer
- fixed-size dir entries (`CAPYFS_NAME_MAX`)

Current runtime rule:
- prefer the logical `DATA` partition handle when probe succeeds
- use raw-disk fallback only when the logical probe fails

## 5. Security and identity stack

- `security/crypt.c`:
  - SHA-256
  - PBKDF2-HMAC-SHA256
  - AES-XTS block encryption layer (4096-byte blocks)
- `security/csprng.c`: internal CSPRNG used for salts and entropy mixing.
- `core/user.c`:
  - `/etc/users.db`
  - per-user salt + PBKDF2 hash
- `core/session.c`: session context (`uid`, `gid`, `home`, `cwd`).

## 6. Shell and user interaction

- `shell/core/shell_main.c`: command parsing, loop, prompt and helpers.
- `shell/commands/*`: command groups (navigation/content/manage/search/help/system).
- `core/system_init.c`:
  - first-boot setup
  - config load/save (`/system/config.ini`)
  - login flow and theme/keyboard handling

## 7. Drivers and hardware support

### 7.1 64-bit path
- Framebuffer console via UEFI handoff
- PCI config space scan (legacy I/O access)
- NVMe single-device bring-up (basic read/write path)
- Initial XHCI controller init/start (enumeration pending)
- Hyper-V support code and serial fallback (COM1)
- Keyboard priority in VM/runtime:
  - `EFI ConIn`
  - PS/2
  - Hyper-V VMBus keyboard (experimental)
  - COM1 fallback

## 8. Build and tooling architecture

- `Makefile` exposes a single supported release track (`UEFI/x64`):
  - `make` / `make all64`, `make iso-uefi`, `make disk-gpt`,
    `make provision-vhd`, `make smoke-x64-cli`, `make inspect-disk`
- Legacy BIOS/x86_32 targets are disabled with explicit guidance.
- Host tools:
  - `tools/host/src/*` (host helpers)
  - `tools/scripts/*` (manifest, GPT provisioning, smoke, disk inspect)
- Unit tests: `make test` (host-mode test binary in `build/tests/unit_tests`).

## 9. Known architectural gaps

- The ISO installer still needs a dedicated smoke for the real install path.
- USB keyboard path is incomplete (XHCI enumeration/HID missing).
- Hyper-V synthetic keyboard path still needs stable VMBus flow.
- The x64 boot path still depends on firmware input in hybrid mode.
- Integrity/authenticated encryption for filesystem metadata is pending.
- Version metadata can diverge between `VERSION.yaml` and C headers.

## 10. Immediate architecture priorities

1. Remove the remaining hybrid boot dependency and complete native x64 input.
2. Add install-path smoke coverage for `ISO -> install -> HDD boot -> login`.
3. Continue removing residual BIOS/x86 legacy code from the repository.
4. Harden CAPYFS for integrity, recovery and scalability.
