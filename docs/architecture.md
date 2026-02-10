# NoirOS Architecture Overview (Current Snapshot)

This document describes the architecture currently implemented in the
repository. NoirOS is in a transition phase and maintains two execution tracks
at the same time.

## 1. Execution tracks

### Track A: BIOS/MBR + x86 32-bit (primary path today)
- Boot chain: `stage1` (MBR) -> `stage2` -> kernel ELF32.
- Installer: NGIS (`src/core/installer_main.c`) partitions disk and writes
  payloads with `bootwriter_install_fresh`.
- Runtime: full path with encrypted NoirFS mount, user login, session, NoirCLI.

### Track B: UEFI/GPT + x86_64 (experimental path)
- Boot chain: `BOOTX64.EFI` (`src/boot/uefi_loader.c`) -> kernel ELF64.
- Loader can read manifest from BOOT GPT partition (raw) or fallback to files
  in ESP.
- Runtime: early 64-bit kernel with framebuffer shell and partial drivers.

## 2. Boot architecture

### 2.1 BIOS path
1. `stage1` is written to LBA0 and patched with `stage2` location.
2. `stage2` is stored in BOOT partition and reads `boot_manifest`.
3. Manifest entries define normal/recovery kernel sectors.
4. Kernel entry reaches `kernel_main` in `src/core/kernel.c`.

### 2.2 UEFI path
1. Firmware starts `BOOTX64.EFI`.
2. Loader resolves manifest/kernel from GPT BOOT partition or ESP fallback.
3. Loader builds a handoff (`include/boot/handoff.h`) with framebuffer,
   memory map and ACPI RSDP info.
4. Control transfers to `kernel_main64` (`src/arch/x86_64/kernel_main.c`).

## 3. Disk layout

### 3.1 BIOS installer layout (MBR)
- Partition 1 (`0xDA`): BOOT (stage2, manifest, kernel payloads).
- Partition 2 (`0x83`): DATA (NoirFS encrypted volume).

### 3.2 UEFI provisioning layout (GPT)
- ESP (FAT32): `EFI/BOOT/BOOTX64.EFI`, kernel/manifest fallback files.
- BOOT (raw): manifest at partition start + kernel payload sectors.
- DATA: reserved for NoirFS path in the 64-bit runtime evolution.

## 4. Storage and filesystem stack

- `fs/block`: generic block-device interface.
- `fs/storage/*`: wrappers (offset/chunk), partition parser, base block device.
- `fs/cache/buffer_cache.c`: buffered block cache and sync.
- `fs/noirfs/noirfs.c`: native NoirFS implementation.
- `fs/vfs/vfs.c`: path resolution, dentries, metadata and permission checks.

NoirFS uses:
- superblock + inode bitmap + block bitmap
- inode with direct pointers + one indirect pointer
- fixed-size dir entries (`NOIRFS_NAME_MAX`)

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

### 7.1 32-bit path
- VGA text console
- PS/2 keyboard
- PIT timer
- ATA PIO storage

### 7.2 64-bit path
- Framebuffer console via UEFI handoff
- PCI config space scan (legacy I/O access)
- NVMe single-device bring-up (basic read/write path)
- Initial XHCI controller init/start (enumeration pending)
- Hyper-V support code and serial fallback (COM1)

## 8. Build and tooling architecture

- `Makefile` exposes both tracks:
  - BIOS: `make`, `make iso`, `make run-*`
  - UEFI: `make all64`, `make iso-uefi`, `make disk-gpt`, `make provision-vhd`
- Host tools:
  - `tools/host/src/*` (grub cfg/build helpers)
  - `tools/scripts/*` (manifest, GPT provisioning, disk inspect)
- Unit tests: `make test` (host-mode test binary in `build/tests/unit_tests`).

## 9. Known architectural gaps

- 64-bit runtime does not yet mirror full 32-bit auth+NoirFS flow.
- USB keyboard path is incomplete (XHCI enumeration/HID missing).
- Hyper-V synthetic keyboard path still needs stable VMBus flow.
- Integrity/authenticated encryption for filesystem metadata is pending.
- Version metadata can diverge between `VERSION.yaml` and C headers.

## 10. Immediate architecture priorities

1. Converge storage/auth path between 32-bit and 64-bit kernels.
2. Finish input stack for UEFI/Hyper-V environments.
3. Harden NoirFS for integrity, recovery and scalability.
4. Move from single-thread execution model to scheduler-ready internals.
