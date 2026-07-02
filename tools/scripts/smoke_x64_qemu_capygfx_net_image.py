#!/usr/bin/env python3
"""
CapyOS x64 QEMU smoke for the Etapa 7 capygfx network sub-resource gate
(Slice 7.5, alpha.303).

Local QEMU+OVMF mirror of `make smoke-x64-vmware-capygfx-net-image`. QEMU is
development feedback only; VMware + UEFI + E1000 remains the official
release-acceptance gate.

Extends the alpha.294 capygfx smoke (window/fill/blit/present/poll + embedded
HTML->pixels pipeline + embedded-PNG decode) with a REAL network fetch of the
page's <img> sub-resource: boots with an E1000 user-net NIC (SLIRP), serves a
real 2x2 PNG (byte-identical to the one alpha.294 embeds) from a local HTTP
server on the QEMU host, and the kernel build hard-codes
CAPYGFX_IMAGE_URL=http://10.0.2.2:<port>/logo.png so the guest's image resolver
(cb_resolve_image_net, gated by CAPYGFX_NET_IMAGE_SMOKE) fetches it for real
through browser_fetch_get + the mixed-content gate
(browser_fetch_subresource_allowed) before decoding it with the same
CapyCodecs adapter used for the embedded-image smoke. It then asserts the same
COM1 success marker the alpha.294 smoke uses:

  * "[smoke] capygfx ready"  (ring-3 capygfx: all graphical syscalls +
     network image fetch + decode + blit, exit 0)

Requires a kernel built with the capygfx smoke flags PLUS the net-image
opt-in, e.g.:
  make all64 PROFILE=full CAPYOS_GFX_SMOKE=1 \
             EXTRA_CFLAGS64='-DCAPYOS_GFX_SMOKE' \
             EXTRA_USERLAND_CFLAGS='-DCAPYGFX_NET_IMAGE_SMOKE -DCAPYGFX_IMAGE_URL=\"http://10.0.2.2:18082/logo.png\"'
  make iso-uefi manifest64
"""

from __future__ import annotations

import argparse
import http.server
import sys
import threading
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "scripts"))

from smoke_x64_common import (  # noqa: E402  (sys.path tweak above)
    boot_with_session,
    cleanup_file,
    create_runtime_ovmf_vars,
    print_log_tail,
    provision_disk,
    resolve_ovmf_or_raise,
    resolve_qemu_binary,
    validate_installed_disk_artifacts,
)

READY_MARKER = "[smoke] capygfx ready"
FAILURE_MARKERS = (
    "panic",
    "capygfx: FAIL",
    "[user_init] capygfx spawn returned without entering Ring 3.",
)

# Same hermetic-endpoint convention as the other Etapa 7 network smokes
# (capybrowse-text: 18080, browser-multifetch: 18081): the guest reaches the
# host through the SLIRP gateway 10.0.2.2. LOCAL_HTTP_PORT MUST match the
# Makefile smoke-x64-qemu-capygfx-net-image target.
LOCAL_HTTP_PORT = 18082
# Byte-identical to main.c's embedded g_logo_png (2x2 RGB PNG), so a passing
# smoke proves the SAME decode path the alpha.294 smoke already validated,
# now fed by real network bytes instead of an embedded array.
LOGO_PNG = bytes([
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a, 0x73,
    0x00, 0x00, 0x00, 0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63,
    0xf8, 0xcf, 0xc0, 0xc0, 0x00, 0xc2, 0x0c, 0xff, 0x81, 0x00, 0x00,
    0x1f, 0xee, 0x05, 0xfb, 0xf1, 0xab, 0xba, 0x77, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
])


class _SmokeHTTPHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802 (stdlib handler method name)
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(LOGO_PNG)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(LOGO_PNG)

    def log_message(self, *args):  # silence per-request logging on stderr
        pass


def start_local_http_server() -> http.server.HTTPServer:
    srv = http.server.HTTPServer(("0.0.0.0", LOCAL_HTTP_PORT), _SmokeHTTPHandler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf", default=None,
                        help="Path to OVMF_CODE.fd (auto-detected if omitted)")
    parser.add_argument("--memory", type=int, default=512)
    parser.add_argument("--timeout", type=float, default=300.0,
                        help="Seconds to wait for the capygfx ready marker")
    parser.add_argument("--log",
                        default="build/ci/smoke_x64_qemu_capygfx_net_image.log")
    parser.add_argument("--debugcon-log",
                        default="build/ci/smoke_x64_qemu_capygfx_net_image.debugcon.log")
    parser.add_argument("--disk",
                        default="build/ci/smoke_x64_qemu_capygfx_net_image.img")
    parser.add_argument("--disk-size", default="2G")
    parser.add_argument("--storage-bus", choices=("sata", "nvme"),
                        default="sata")
    parser.add_argument("--keep-disk", action="store_true")
    parser.add_argument("--volume-key", default="CAPYOS-SMOKE-KEY-2026-0001")
    parser.add_argument("--keyboard-layout", default="us")
    parser.add_argument("--language", default="en")
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    log_path = (REPO_ROOT / args.log).resolve()
    debugcon_log = (REPO_ROOT / args.debugcon_log).resolve()
    disk_path = (REPO_ROOT / args.disk).resolve()
    for target in (log_path, debugcon_log, disk_path):
        target.parent.mkdir(parents=True, exist_ok=True)
    debugcon_log.write_bytes(b"")

    try:
        qemu_bin = resolve_qemu_binary(args.qemu)
        ovmf_code, ovmf_vars_template = resolve_ovmf_or_raise(args.ovmf)
        bootx64, kernel, manifest = validate_installed_disk_artifacts(REPO_ROOT)
    except FileNotFoundError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2

    provision_disk(
        repo_root=REPO_ROOT,
        disk_path=disk_path,
        disk_size=args.disk_size,
        bootx64=bootx64,
        kernel=kernel,
        manifest=manifest,
        keyboard_layout=args.keyboard_layout,
        language=args.language,
        volume_key=args.volume_key,
    )

    ovmf_vars_runtime = create_runtime_ovmf_vars(log_path, ovmf_vars_template)

    http_server = start_local_http_server()
    print(f"[info] local HTTP endpoint up on host :{LOCAL_HTTP_PORT} "
          f"(guest fetches http://10.0.2.2:{LOCAL_HTTP_PORT}/logo.png via SLIRP)")
    print(f"[info] launching QEMU (E1000 user-net); serial+stdout -> {log_path}")
    session = boot_with_session(
        qemu_bin=qemu_bin,
        ovmf_code=ovmf_code,
        ovmf_vars_runtime=ovmf_vars_runtime,
        disk_path=disk_path,
        log_path=log_path,
        debugcon_log=debugcon_log,
        memory_mb=args.memory,
        storage_bus=args.storage_bus,
        verbose=args.verbose,
        networking=True,
    )

    rc = 1
    try:
        print(f"[info] waiting for capygfx ready (<= {args.timeout:.0f}s)")
        session.wait_for(READY_MARKER, timeout=args.timeout)
        print(f"[ok]   + {READY_MARKER!r}")
        print("[ok] qemu-capygfx-net-image smoke passed")
        rc = 0
    except (TimeoutError, RuntimeError) as exc:
        print(f"[err] qemu-capygfx-net-image smoke failed: {exc}", file=sys.stderr)
        captured = session.text()
        for marker in FAILURE_MARKERS:
            if marker in captured:
                print(f"      failure marker present: {marker!r}",
                      file=sys.stderr)
        print_log_tail(log_path)
    finally:
        session.stop()
        http_server.shutdown()
        http_server.server_close()
        cleanup_file(ovmf_vars_runtime)
        if not args.keep_disk:
            cleanup_file(disk_path)

    return rc


if __name__ == "__main__":
    sys.exit(main())
