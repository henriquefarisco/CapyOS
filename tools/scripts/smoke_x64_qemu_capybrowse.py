#!/usr/bin/env python3
"""
CapyOS x64 QEMU smoke for the Etapa 6 CapyBrowse Text gate.

Local QEMU+OVMF mirror of `make smoke-x64-vmware-capybrowse-text`. QEMU is
development feedback only; VMware + UEFI + E1000 remains the official
release-acceptance gate (see docs/operations/etapa-6-external-validation-playbook.md).

Boots the provisioned GPT disk with an E1000 user-net NIC so the guest can
acquire a DHCP lease (SLIRP). This gate is hermetic: it serves a tiny HTML page
from a local HTTP server on the QEMU host and the kernel build hard-codes
CAPYOS_CAPYBROWSE_URL=http://10.0.2.2:<port>/ so the guest fetches it through
the SLIRP gateway 10.0.2.2 -- an IP literal (no DNS; CapyOS has no active
resolver yet) over plain HTTP (no TLS trust anchors). It then asserts the COM1
success marker:

  * "[smoke] capybrowse-text ready"  (ring-3 capybrowse fetched + rendered + exit 0)

The "[net] DHCP: lease acquired." bootstrap marker is reported informationally:
under TCG the lease frequently lands async, after the synchronous boot-time
acquire window, and capybrowse's retry loop absorbs it.

Requires a kernel built with the capybrowse-text smoke flags, e.g.:
  make all64 PROFILE=full CAPYOS_TLS_USERLAND_HANDSHAKE=1 \
             CAPYOS_CAPYBROWSE_SMOKE=1 EXTRA_CFLAGS64='-DCAPYOS_CAPYBROWSE_SMOKE'
  make iso-uefi manifest64
and outbound network reachability from the QEMU host (SLIRP NAT + DNS).

Pass criterion (matched against the COM1 serial log):
  * "[smoke] capybrowse-text ready" present (ring-3 capybrowse exit 0).
Failure handling:
  Timeout or early QEMU exit -> exit 1 + tail of the serial log to stderr;
  the capybrowse spawn-return / panic markers are reported when present.
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

DHCP_MARKER = "[net] DHCP: lease acquired."
READY_MARKER = "[smoke] capybrowse-text ready"
FAILURE_MARKERS = (
    "panic",
    "[user_init] capybrowse spawn returned without entering Ring 3.",
)

# Hermetic local HTTP endpoint served on the QEMU host. The guest reaches the
# host through the SLIRP gateway 10.0.2.2, and the kernel build hard-codes
# CAPYOS_CAPYBROWSE_URL=http://10.0.2.2:<LOCAL_HTTP_PORT>/ (see the Makefile
# target). Using the gateway IP literal sidesteps DNS entirely (CapyOS has no
# active resolver yet -- only a DHCP-seeded cache, see capy_net_resolve.c), and
# plain HTTP sidesteps TLS trust anchors. LOCAL_HTTP_PORT MUST match the Makefile
# CAPYBROWSE_SMOKE_HTTP_PORT. The page is tiny but exercises title + body + link
# extraction in capy_html_to_text.
LOCAL_HTTP_PORT = 18080
LOCAL_HTTP_BODY = (
    b"<!doctype html><html><head><title>CapyOS QEMU capybrowse smoke</title>"
    b"</head><body><h1>capybrowse text gate</h1>"
    b"<p>Local hermetic smoke endpoint OK.</p>"
    b"<a href=\"/next\">next link</a></body></html>"
)


class _SmokeHTTPHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802 (stdlib handler method name)
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(LOCAL_HTTP_BODY)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(LOCAL_HTTP_BODY)

    def log_message(self, *args):  # silence per-request logging on stderr
        pass


def start_local_http_server() -> http.server.HTTPServer:
    # Bind 0.0.0.0 so the SLIRP gateway (guest -> 10.0.2.2 -> host) reaches it.
    srv = http.server.HTTPServer(("0.0.0.0", LOCAL_HTTP_PORT), _SmokeHTTPHandler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--ovmf", default=None,
                        help="Path to OVMF_CODE.fd (auto-detected if omitted)")
    parser.add_argument("--memory", type=int, default=512)
    parser.add_argument("--timeout", type=float, default=420.0,
                        help="Seconds to wait for the capybrowse-text ready "
                             "marker (covers TCG boot + async DHCP + TLS fetch)")
    parser.add_argument("--log",
                        default="build/ci/smoke_x64_qemu_capybrowse.log",
                        help="Combined QEMU stdout + COM1 serial log")
    parser.add_argument("--debugcon-log",
                        default="build/ci/smoke_x64_qemu_capybrowse.debugcon.log")
    parser.add_argument("--disk",
                        default="build/ci/smoke_x64_qemu_capybrowse.img")
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
          f"(guest fetches http://10.0.2.2:{LOCAL_HTTP_PORT}/ via SLIRP)")
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
        # The definitive success signal is the capybrowse-text ready marker
        # (ring-3 capybrowse fetched + rendered + exited 0). The bootstrap
        # "[net] DHCP: lease acquired." marker only fires when the synchronous
        # boot-time DHCP acquire wins its 2.5s window; under TCG the lease often
        # lands async after that window and capybrowse's retry loop absorbs it,
        # so it is reported informationally rather than gated.
        print(f"[info] waiting for capybrowse-text ready (<= {args.timeout:.0f}s)")
        session.wait_for(READY_MARKER, timeout=args.timeout)
        captured = session.text()
        if DHCP_MARKER in captured:
            print(f"[ok]   + {DHCP_MARKER!r}")
        else:
            print(f"[info]  ({DHCP_MARKER!r} not observed; async DHCP lease "
                  "absorbed by the capybrowse retry loop)")
        print(f"[ok]   + {READY_MARKER!r}")
        print("[ok] qemu-capybrowse-text smoke passed")
        rc = 0
    except (TimeoutError, RuntimeError) as exc:
        print(f"[err] qemu-capybrowse-text smoke failed: {exc}", file=sys.stderr)
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
