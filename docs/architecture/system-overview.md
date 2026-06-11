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
- The ISO installer path now has a dedicated end-to-end smoke that covers
  install -> reboot from HDD -> login -> persistence, and the Gen2 path has
  already been validated; broader hardware coverage remains future hardening.

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

## 7. Network stack

### 7.1 Protocol layers (x86_64 only)

- **ARP** (`src/net/protocols/stack_arp.c`): request/reply, cache (LRU), next-hop routing.
- **IPv4** (`src/net/protocols/stack_ipv4.c`): checksum, protocol dispatch (ICMP/UDP/TCP).
- **ICMP** (`src/net/protocols/stack_icmp.c`): echo request/reply (used by `hey`).
- **UDP** (`src/net/protocols/stack_udp.c`): used for DNS queries.
- **TCP** (`src/net/protocols/tcp.c`):
  - pseudo-header checksum (corrected 2026-04-19: raw word values, not byte-swapped)
  - three-way handshake, RST handling, FIN/ACK teardown
  - SYN retransmission with exponential backoff (1 s / 2 s / 4 s ...)
  - receive buffer compaction to prevent TLS handshake corruption
  - up to 8 simultaneous connections (`TCP_MAX_CONNECTIONS`)
- **TLS**: the **kernel-side** client (`src/security/tls.c`, BearSSL) is
  real and in production — TLS 1.2, 146 trust anchors, ALPN, SNI, hostname
  verification, cipher reporting — and is what `http_get`/capypkg use over
  HTTPS today. The **userland** `libcapy-tls` (`userland/lib/capylibc-tls/`)
  is the Etapa 5 target: its real BearSSL handshake is already in-tree but
  gated behind `CAPYOS_TLS_USERLAND_HANDSHAKE` (default OFF → fail-closed),
  so ring-3 HTTPS still reports unsupported until Etapa 5 closes with the
  external smoke. See
  [`etapa-5-tls-userland-readiness.md`](etapa-5-tls-userland-readiness.md).
- **HTTP/HTTPS** (`src/net/services/http.c`):
  - `http_get()` follows up to 5 redirects (301/302/303/307/308)
  - resolves absolute, protocol-relative, origin-relative and document-relative `Location:` values
  - `net-fetch` CLI command: shows status, content-type, TLS state, body preview and diagnostics
- **DNS** (`src/net/services/dns.c`): UDP/53, single A-record query, 2.5 s timeout.
- **DHCP** (`src/net/services/dhcp.c`): DISCOVER/OFFER/REQUEST/ACK, host-byte-order lease.

### 7.2 Socket layer

- `src/net/services/socket.c`: BSD-like `socket()/connect()/send()/recv()/close()`.
- Port byte order: `sin_port` is network order (BSD convention); `socket_connect` converts
  to host order before calling `tcp_open` (which will call `htons` internally).

### 7.3 Yield hook

- `net_stack_set_yield_hook(fn)` registered by `desktop_runtime` so the compositor
  renders the cursor on each `net_stack_delay_approx_1ms()` call, keeping the mouse
  responsive during blocking network operations.

## 8. Drivers and hardware support

### 7.1 64-bit path
- Framebuffer console via UEFI handoff
- PCI config space scan (legacy I/O access)
- NVMe single-device bring-up (basic read/write path)
- XHCI enumeration + USB HID Slice 3D implemented by code/host tests; VMware validation pending
- Hyper-V support code and serial fallback (COM1)
- Keyboard priority in VM/runtime:
  - USB HID via XHCI (experimental, pending external Slice 3D validation)
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

- The ISO installer smoke now exists for the real install path; remaining work
  is broader hardware coverage and continued native-input hardening.
- USB keyboard path has Slice 3D code/host tests, but still requires external VMware validation before reducing firmware input reliance.
- Hyper-V synthetic keyboard path still needs stable VMBus flow.
- The x64 boot path still depends on firmware input in hybrid mode.
- Integrity/authenticated encryption for filesystem metadata is pending.
- Version metadata can diverge between `VERSION.yaml` and C headers.
- Browser path: `CapyBrowse Text` is planned first as a native text-mode
  HTTP/HTTPS diagnostic browser for simple sites; the graphical browser then
  grows static HTTPS rendering, image decode, cache and forms. JavaScript,
  multimedia and any Mozilla/Firefox-style port remain later-stage work gated by
  sandboxing, CapyLX/Linux ABI and hardening.
- High-level engines should be decoupled from the base system: CapyLang,
  browser core, package format, widget model, codecs and benchmarks are allowed
  to evolve outside this repository only through the contracts in
  `docs/reference/integration/`.
- The current UI boundary is semi-decoupled, not a generic third-party UI SDK:
  CapyUI owns the desktop session/widget model, while CapyOS owns compositor,
  windows, input, surfaces, fonts, damage tracking and final activation. Any
  alternative UI must be introduced through a versioned integration contract,
  package/activation fallback policy and official gate rather than by extending
  the in-tree fallback.

## 10. Immediate architecture priorities

Reordered on 2026-05-15 to match the ROI-driven sequence in `docs/plans/active/capyos-master-plan.md` (target audience: common desktop user). **Etapas 1-4 are closed** (Etapa 3 in `alpha.253`, Etapa 4 in `alpha.262`); **Etapa 5 (TLS userland real) is the active stage.** Stages 3-4 are retained below as a description of delivered scope; `docs/plans/STATUS.md` is the authoritative live status.

1. **Stage 3 (Driver framework + USB HID + storage) — closed (`alpha.253`):** XHCI/USB HID Slice 3D validated externally; AHCI/NVMe error handling and the device-manager fallback policy delivered (follow-ups 3F-3J are non-blocking).
2. **Stage 4 (CapyDisplay 2D + scheduler) — closed (`alpha.262`):** damage-tracked 2D layer and cooperative scheduler/multithread runtime delivered and externally validated (Fase F).
3. **Stage 5 (TLS userland real):** the real BearSSL `libcapy-tls` handshake (SNI + hostname verification + timeout, reusing the kernel trust bundle) is in-tree behind `CAPYOS_TLS_USERLAND_HANDSHAKE` (default OFF); the remaining work is external build + smoke validation to flip ring-3 HTTPS from unsupported to supported.
4. **Stage 6 (Apps maduros) and Stage 7 (Browser usável):** integrate decoupled `CapyBrowse Text`/HTML core for simple text sites and network diagnostics first, then wire codec/display-list contracts into the graphical renderer, add HTTP cache, streaming render and basic forms. Closes `feature/browser-internet-improvements`.
5. **Continued architectural hygiene (cross-stage):**
   - Remove remaining hybrid boot dependency and complete native x64 input.
   - Broaden hardware coverage for the official `ISO -> install -> HDD boot -> login` path.
   - Remove residual BIOS/x86 legacy code from the repository.
   - Harden CAPYFS for integrity, recovery and scalability.
