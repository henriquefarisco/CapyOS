#!/usr/bin/env python3
"""
CapyOS x64 QEMU smoke for the Etapa 7 browser-multifetch gate (Slice 7.5).

Local QEMU+OVMF mirror of `make smoke-x64-vmware-browser-multifetch`. QEMU is
development feedback only; VMware + UEFI + E1000 remains the official
release-acceptance gate.

Boots the provisioned GPT disk with an E1000 user-net NIC so the guest can
acquire a DHCP lease (SLIRP). This gate is hermetic: it serves a tiny
CACHEABLE HTML page (Cache-Control: max-age=<big>) from a local HTTP server on
the QEMU host, and the kernel build hard-codes
CAPYMULTIFETCH_URL=http://10.0.2.2:<port>/ so the guest fetches it through the
SLIRP gateway 10.0.2.2 -- an IP literal (no DNS; CapyOS has no active resolver
yet) over plain HTTP (no TLS trust anchors; see capymultifetch/main.c for why
this smoke deliberately does not exercise the HTTPS-first policy). The ring-3
`/bin/capymultifetch` program fetches this URL TWICE through the SAME
persistent `browser_fetch_ctx` and exits 0 iff the 2nd visit was served from
the in-process cache with NO 2nd network transport call -- the runtime proof
of the Etapa 7 "cache acelera a 2a visita" criterion. It then asserts the COM1
success marker:

  * "[smoke] browser-multifetch ready"  (ring-3 capymultifetch: fetch x2,
     2nd served from cache, exit 0)

The "[net] DHCP: lease acquired." bootstrap marker is reported informationally:
under TCG the lease frequently lands async, after the synchronous boot-time
acquire window, and capymultifetch's retry loop absorbs it.

The local server also counts GET requests received: the smoke additionally
asserts (informationally, via the log) that exactly ONE request reached the
host -- the same fact the kernel-side marker already gates, cross-checked here
independently of the guest's own bookkeeping.

Requires a kernel built with the browser-multifetch smoke flags, e.g.:
  make all64 PROFILE=full CAPYOS_TLS_USERLAND_HANDSHAKE=1 \
             CAPYOS_MULTIFETCH_SMOKE=1 EXTRA_CFLAGS64='-DCAPYOS_MULTIFETCH_SMOKE'
  make iso-uefi manifest64
and outbound network reachability from the QEMU host is NOT required (the
endpoint is the local hermetic server via SLIRP).

Pass criterion (matched against the COM1 serial log):
  * "[smoke] browser-multifetch ready" present (ring-3 capymultifetch exit 0).
Failure handling:
  Timeout or early QEMU exit -> exit 1 + tail of the serial log to stderr;
  the capymultifetch spawn-return / panic markers are reported when present.
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
READY_MARKER = "[smoke] browser-multifetch ready"
FAILURE_MARKERS = (
    "panic",
    "[user_init] capymultifetch spawn returned without entering Ring 3.",
    "multifetch: 1st fetch failed",
    "multifetch: cache did not short-circuit",
)

# Hermetic local HTTP endpoint served on the QEMU host. The guest reaches the
# host through the SLIRP gateway 10.0.2.2, and the kernel build hard-codes
# CAPYMULTIFETCH_URL=http://10.0.2.2:<LOCAL_HTTP_PORT>/ (see the Makefile
# target). LOCAL_HTTP_PORT MUST match the Makefile
# CAPYMULTIFETCH_SMOKE_HTTP_PORT. Cache-Control: max-age is what lets the
# guest's cache serve the 2nd visit without a 2nd request.
LOCAL_HTTP_PORT = 18081
LOCAL_HTTP_BODY = (
    b"<!doctype html><html><head><title>CapyOS multifetch smoke</title>"
    b"</head><body><h1>cacheable page</h1>"
    b"<p>Local hermetic multifetch smoke endpoint OK.</p></body></html>"
)

_request_count_lock = threading.Lock()
_request_count = 0


class _SmokeHTTPHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802 (stdlib handler method name)
        global _request_count
        with _request_count_lock:
            _request_count += 1
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(LOCAL_HTTP_BODY)))
        # Long max-age: the guest's 2nd visit (moments later) must still be
        # fresh. No Vary, no no-store/no-cache -> cacheable per RFC 7234.
        self.send_header("Cache-Control", "max-age=86400")
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
                        help="Seconds to wait for the browser-multifetch ready "
                             "marker (covers TCG boot + async DHCP + 2x fetch)")
    parser.add_argument("--log",
                        default="build/ci/smoke_x64_qemu_browser_multifetch.log")
    parser.add_argument("--debugcon-log",
                        default="build/ci/smoke_x64_qemu_browser_multifetch.debugcon.log")
    parser.add_argument("--disk",
                        default="build/ci/smoke_x64_qemu_browser_multifetch.img")
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
          f"(guest fetches http://10.0.2.2:{LOCAL_HTTP_PORT}/ via SLIRP, "
          "Cache-Control: max-age=86400)")
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
        print(f"[info] waiting for browser-multifetch ready (<= {args.timeout:.0f}s)")
        session.wait_for(READY_MARKER, timeout=args.timeout)
        captured = session.text()
        if DHCP_MARKER in captured:
            print(f"[ok]   + {DHCP_MARKER!r}")
        else:
            print(f"[info]  ({DHCP_MARKER!r} not observed; async DHCP lease "
                  "absorbed by the capymultifetch retry loop)")
        print(f"[ok]   + {READY_MARKER!r}")
        with _request_count_lock:
            seen = _request_count
        if seen == 1:
            print(f"[ok]   + host observed exactly 1 HTTP request "
                  f"(2nd visit served from the guest's cache, as expected)")
        else:
            print(f"[warn]  host observed {seen} HTTP request(s) "
                  "(expected exactly 1 -- kernel marker is still authoritative)")
        print("[ok] qemu-browser-multifetch smoke passed")
        rc = 0
    except (TimeoutError, RuntimeError) as exc:
        print(f"[err] qemu-browser-multifetch smoke failed: {exc}", file=sys.stderr)
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
