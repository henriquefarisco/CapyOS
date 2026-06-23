#!/usr/bin/env python3
"""
Generic CapyOS x64 QEMU+OVMF runtime smoke: boot a built kernel and assert a
COM1 marker. Development feedback / CI pre-flight only; VMware + UEFI + E1000
remains the official release-acceptance gate.

Unlike smoke_x64_qemu_capybrowse.py (which adds a hermetic HTTP server + an
E1000 NIC for the browser path), this driver is marker-only: it provisions the
GPT disk from the already-built artifacts, boots it, and waits for a `--marker`
string on the serial log. It suits in-kernel boot markers that fire before login
and need no network -- e.g. the Etapa 6 apps-basic-roundtrip orchestrator
(`[smoke] apps-basic-roundtrip ready`).

The kernel must already be built with the relevant gate flags (the Makefile
target does that) and `make iso-uefi manifest64` run, so the installed-disk
artifacts exist.

Pass criterion: `--marker` observed on the COM1 serial log within `--timeout`.
Failure: timeout / early QEMU exit / any `--fail-marker` present -> exit 1 +
serial-log tail to stderr. Missing build artifacts -> exit 2.
"""

from __future__ import annotations

import argparse
import sys
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

DEFAULT_FAIL_MARKERS = ("panic", "KERNEL PANIC", "#PF", "#GP")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--marker", required=True,
                   help="COM1 success marker string to wait for")
    p.add_argument("--fail-marker", action="append", default=[],
                   help="Extra marker that means failure (repeatable; the "
                        "common panic/fault markers are always checked)")
    p.add_argument("--timeout", type=float, default=180.0,
                   help="Seconds to wait for the marker (TCG boot is slow)")
    p.add_argument("--qemu", default="qemu-system-x86_64")
    p.add_argument("--ovmf", default=None,
                   help="Path to OVMF_CODE.fd (auto-detected if omitted)")
    p.add_argument("--memory", type=int, default=512)
    p.add_argument("--storage-bus", choices=("sata", "nvme"), default="sata")
    p.add_argument("--networking", action="store_true",
                   help="Attach an E1000 user-net NIC (off by default; the "
                        "in-kernel boot markers need no network)")
    p.add_argument("--log", default="build/ci/smoke_x64_qemu_marker.log",
                   help="Combined QEMU stdout + COM1 serial log")
    p.add_argument("--debugcon-log",
                   default="build/ci/smoke_x64_qemu_marker.debugcon.log")
    p.add_argument("--disk", default="build/ci/smoke_x64_qemu_marker.img")
    p.add_argument("--disk-size", default="2G")
    p.add_argument("--keep-disk", action="store_true")
    p.add_argument("--volume-key", default="CAPYOS-SMOKE-KEY-2026-0001")
    p.add_argument("--keyboard-layout", default="us")
    p.add_argument("--language", default="en")
    p.add_argument("--verbose", action="store_true")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    fail_markers = tuple(DEFAULT_FAIL_MARKERS) + tuple(args.fail_marker)

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

    print(f"[info] launching QEMU (networking={args.networking}); "
          f"serial+stdout -> {log_path}")
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
        networking=args.networking,
    )

    rc = 1
    try:
        print(f"[info] waiting for {args.marker!r} (<= {args.timeout:.0f}s)")
        session.wait_for(args.marker, timeout=args.timeout)
        print(f"[ok]   + {args.marker!r}")
        print("[ok] qemu-marker smoke passed")
        rc = 0
    except (TimeoutError, RuntimeError) as exc:
        print(f"[err] qemu-marker smoke failed: {exc}", file=sys.stderr)
        captured = session.text()
        for marker in fail_markers:
            if marker in captured:
                print(f"      failure marker present: {marker!r}",
                      file=sys.stderr)
        print_log_tail(log_path)
    finally:
        session.stop()
        cleanup_file(ovmf_vars_runtime)
        if not args.keep_disk:
            cleanup_file(disk_path)

    return rc


if __name__ == "__main__":
    sys.exit(main())
