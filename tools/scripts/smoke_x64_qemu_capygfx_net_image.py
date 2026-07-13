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

``--real-sites-dir`` reuses the same VM/server harness for the browser
liveness regression. The guest requests ``/real-site`` three times; the host
serves authenticated captures of YouTube, Wikipedia and Tumblr in that order
and waits for the browser's second delayed window-present heartbeat.
"""

from __future__ import annotations

import argparse
import hashlib
import http.server
import json
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
SITE_READY_MARKER = (
    "[smoke] capygfx site redirect+css+image+link+history ready"
)
REAL_SITES_LIVENESS_MARKER = (
    "[smoke] capygfx real-sites window alive 2"
)
FAILURE_MARKERS = (
    "panic",
    "capygfx: FAIL",
    "capygfx-site:",
    "capygfx-real-sites:",
    "[user-fault]",
    "user exception",
    "browser runtime degraded",
    "browser window closed",
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

SITE_HTML = (
    b"<html><head><title>Capy site</title>"
    b"<link rel=\"stylesheet\" href=\"/style.css\"></head>"
    b"<body><h1>CapyBrowser</h1><p>static site acceptance</p>"
    b"<img src=\"/logo.png\" alt=\"logo\">"
    b"<a href=\"/next\">next page</a></body></html>"
)
SITE_CSS = b"body { background-color: #ffffff; } h1 { color: #1a4fd0; }"
NEXT_HTML = b"<html><body><h1>Next page</h1><p>history target</p></body></html>"
REQUEST_COUNTS: dict[str, int] = {}
REAL_SITE_ORDER = ("youtube", "wikipedia", "tumblr")
REAL_SITE_PAYLOADS: list[tuple[str, bytes]] = []
REAL_SITE_SERVED: list[str] = []
MAX_REAL_SITE_BYTES = 512 * 1024


def _resolve_repo_path(value: str) -> Path:
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = REPO_ROOT / path
    return path.resolve()


def load_real_site_payloads(directory: Path) -> list[tuple[str, bytes]]:
    """Load and authenticate the capture tool's three bounded fixtures."""
    manifest_path = directory / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError(
            f"cannot read real-site manifest {manifest_path}: {exc}"
        ) from exc

    if manifest.get("schema_version") != 1:
        raise ValueError("real-site manifest schema_version must be 1")
    records = manifest.get("sites")
    if not isinstance(records, list):
        raise ValueError("real-site manifest sites must be a list")
    by_name = {
        record.get("name"): record
        for record in records
        if isinstance(record, dict) and isinstance(record.get("name"), str)
    }

    payloads: list[tuple[str, bytes]] = []
    for name in REAL_SITE_ORDER:
        record = by_name.get(name)
        if record is None:
            raise ValueError(f"real-site manifest is missing {name!r}")
        filename = record.get("file")
        if filename != f"{name}.html":
            raise ValueError(
                f"real-site {name!r} has unexpected file {filename!r}"
            )
        path = directory / filename
        try:
            payload = path.read_bytes()
        except OSError as exc:
            raise ValueError(f"cannot read real-site fixture {path}: {exc}") from exc
        if not payload:
            raise ValueError(f"real-site fixture {path} is empty")
        if len(payload) > MAX_REAL_SITE_BYTES:
            raise ValueError(
                f"real-site fixture {path} exceeds {MAX_REAL_SITE_BYTES} bytes"
            )
        if record.get("served_bytes") != len(payload):
            raise ValueError(
                f"real-site fixture {name!r} size does not match manifest"
            )
        expected_hash = record.get("served_sha256")
        actual_hash = hashlib.sha256(payload).hexdigest()
        if expected_hash != actual_hash:
            raise ValueError(f"real-site fixture {name!r} SHA-256 does not match")
        payloads.append((name, payload))
    return payloads


class _SmokeHTTPHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _count(self, path: str) -> None:
        REQUEST_COUNTS[path] = REQUEST_COUNTS.get(path, 0) + 1

    def _fixed(self, status: int, content_type: str, payload: bytes,
               *, cache: bool = True) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "max-age=300" if cache else "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)
        self.close_connection = True

    def do_GET(self):  # noqa: N802 (stdlib handler method name)
        path = self.path.split("?", 1)[0]
        self._count(path)
        if path == "/real-site":
            index = REQUEST_COUNTS[path] - 1
            if index >= len(REAL_SITE_PAYLOADS):
                self._fixed(410, "text/plain", b"real-site sequence exhausted",
                            cache=False)
                return
            name, payload = REAL_SITE_PAYLOADS[index]
            REAL_SITE_SERVED.append(name)
            self._fixed(200, "text/html; charset=utf-8", payload, cache=False)
            return
        if path == "/start":
            self.send_response(302)
            self.send_header("Location", "/page")
            self.send_header("Content-Length", "0")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            return
        if path == "/page":
            # Deliberately frame the top-level document as chunked so this is a
            # release gate for the strict userland decoder, not only routing.
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Transfer-Encoding", "chunked")
            self.send_header("Cache-Control", "max-age=300")
            self.send_header("Connection", "close")
            self.end_headers()
            split = len(SITE_HTML) // 2
            for chunk in (SITE_HTML[:split], SITE_HTML[split:]):
                self.wfile.write(f"{len(chunk):X}\r\n".encode("ascii"))
                self.wfile.write(chunk + b"\r\n")
            self.wfile.write(b"0\r\nX-Smoke-Trailer: ok\r\n\r\n")
            self.close_connection = True
            return
        if path == "/style.css":
            self._fixed(200, "text/css", SITE_CSS)
            return
        if path == "/logo.png":
            self._fixed(200, "image/png", LOGO_PNG)
            return
        if path == "/next":
            self._fixed(200, "text/html; charset=utf-8", NEXT_HTML)
            return
        self._fixed(404, "text/plain", b"not found", cache=False)

    def log_message(self, *args):  # silence per-request logging on stderr
        pass


def start_local_http_server() -> http.server.HTTPServer:
    REQUEST_COUNTS.clear()
    REAL_SITE_SERVED.clear()
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
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--site", action="store_true",
        help="Run the full static-site navigation/toolbar acceptance flow",
    )
    mode.add_argument(
        "--real-sites-dir",
        help=(
            "Directory containing youtube.html, wikipedia.html, tumblr.html "
            "and manifest.json from capture_capygfx_real_sites.py"
        ),
    )
    return parser.parse_args()


def main() -> int:
    global REAL_SITE_PAYLOADS
    args = parse_args()

    if args.real_sites_dir:
        real_sites_dir = _resolve_repo_path(args.real_sites_dir)
        try:
            REAL_SITE_PAYLOADS = load_real_site_payloads(real_sites_dir)
        except ValueError as exc:
            print(f"[err] {exc}", file=sys.stderr)
            return 2
        print(f"[info] loaded real-site fixtures from {real_sites_dir}")
        for name, payload in REAL_SITE_PAYLOADS:
            print(
                f"[info]   {name}: {len(payload)} bytes, "
                f"sha256={hashlib.sha256(payload).hexdigest()}"
            )
    else:
        REAL_SITE_PAYLOADS = []

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
    endpoint = (
        "/real-site" if args.real_sites_dir
        else "/start" if args.site
        else "/logo.png"
    )
    print(f"[info] local HTTP endpoint up on host :{LOCAL_HTTP_PORT} "
          f"(guest fetches http://10.0.2.2:{LOCAL_HTTP_PORT}{endpoint} via SLIRP)")
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
        marker = (
            REAL_SITES_LIVENESS_MARKER if args.real_sites_dir
            else SITE_READY_MARKER if args.site
            else READY_MARKER
        )
        print(f"[info] waiting for capygfx ready (<= {args.timeout:.0f}s)")
        session.wait_for(marker, timeout=args.timeout)
        print(f"[ok]   + {marker!r}")
        captured = session.text()
        failures = [item for item in FAILURE_MARKERS if item in captured]
        if failures:
            raise RuntimeError(
                f"failure markers present before liveness: {failures}"
            )
        if args.real_sites_dir:
            expected = list(REAL_SITE_ORDER)
            if REAL_SITE_SERVED != expected:
                raise RuntimeError(
                    f"real-site sequence mismatch: expected {expected}, "
                    f"served {REAL_SITE_SERVED}"
                )
            if REQUEST_COUNTS.get("/real-site", 0) != len(expected):
                raise RuntimeError(
                    "guest did not fetch /real-site exactly three times"
                )
            print(f"[ok] real-site sequence: {REAL_SITE_SERVED}")
            print("[ok] qemu-capygfx-real-sites liveness smoke passed")
        elif args.site:
            required = ("/start", "/page", "/style.css", "/logo.png", "/next")
            missing = [path for path in required if REQUEST_COUNTS.get(path, 0) < 1]
            if missing:
                raise RuntimeError(f"guest did not fetch required paths: {missing}")
            if REQUEST_COUNTS.get("/next", 0) < 2:
                raise RuntimeError("reload did not refetch /next")
            print(f"[ok] endpoint coverage: {REQUEST_COUNTS}")
            print("[ok] qemu-capygfx-static-site smoke passed")
        else:
            print("[ok] qemu-capygfx-net-image smoke passed")
        rc = 0
    except (TimeoutError, RuntimeError) as exc:
        mode_name = (
            "qemu-capygfx-real-sites" if args.real_sites_dir
            else "qemu-capygfx-static-site" if args.site
            else "qemu-capygfx-net-image"
        )
        print(f"[err] {mode_name} smoke failed: {exc}", file=sys.stderr)
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
