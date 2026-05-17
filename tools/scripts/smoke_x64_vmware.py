#!/usr/bin/env python3
"""CapyOS VMware+E1000 DHCP smoke harness.

This script intentionally orchestrates an external VMware VM and validates its
serial/debug console log. It does not create VMware infrastructure by itself;
release CI or the operator provides either a vmrun-compatible .vmx path or a
govc VM name plus an accessible serial log source.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

from smoke_marker_policy import (
    first_failure_marker,
    markers_in_order,
    self_test as marker_policy_self_test,
    unique_markers,
)

DEFAULT_MARKERS = (
    "[net] DHCP: lease acquired.",
)


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def tool_path(name: str) -> str | None:
    path = shutil.which(name)
    return path if path else None


def run_command(args: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=str(cwd) if cwd else None,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def resolve_iso(repo_root: Path, requested: Path | None) -> Path | None:
    if requested is None:
        return None
    if requested.exists():
        return requested
    sidecar = repo_root / "build/CapyOS-Installer-UEFI.last-built.txt"
    if sidecar.exists():
        recorded = sidecar.read_text(encoding="utf-8", errors="ignore").strip()
        if recorded:
            path = Path(recorded)
            if not path.is_absolute():
                path = repo_root / path
            if path.exists():
                return path.resolve()
    return requested


def require_artifacts(repo_root: Path, iso: Path | None, disk: Path | None) -> int:
    required = [
        repo_root / "build/boot/uefi_loader.efi",
        repo_root / "build/capyos64.bin",
        repo_root / "build/manifest.bin",
    ]
    if iso is not None:
        required.append(resolve_iso(repo_root, iso))
    if disk is not None:
        required.append(disk)
    missing = [path for path in required if not path.exists()]
    if missing:
        for path in missing:
            print(f"[err] artefato ausente: {path}", file=sys.stderr)
        return 1
    return 0


def read_log(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="latin-1", errors="replace")


def write_tail(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text[-12000:], encoding="utf-8")


def run_self_test() -> int:
    policy_ok, policy_message = marker_policy_self_test()
    if not policy_ok:
        return fail("self-test smoke_marker_policy: " + policy_message)
    markers = ("alpha", "beta", "gamma")
    if not markers_in_order("ALPHA\nnoise\nbeta\ngamma", markers):
        return fail("self-test: markers em ordem foram rejeitados")
    if markers_in_order("beta\nalpha\ngamma", markers):
        return fail("self-test: markers fora de ordem foram aceitos")
    if markers_in_order("alpha\ngamma", markers):
        return fail("self-test: marker ausente foi aceito")
    if not markers_in_order("ready\nnoise\nready", ("ready", "ready")):
        return fail("self-test: markers repetidos em ordem foram rejeitados")
    if markers_in_order("ready", ("ready", "ready")):
        return fail("self-test: marker repetido sem segunda ocorrencia foi aceito")
    if unique_markers(("alpha", "beta", "alpha", "gamma")) != markers:
        return fail("self-test: markers duplicados nao foram deduplicados em ordem")
    if first_failure_marker("alpha\npanic:\nbeta\ngamma") != "panic:":
        return fail("self-test: marker de falha nao foi detectado")
    print("[ok] self-test smoke_x64_vmware markers-in-order passed")
    return 0


def wait_for_markers(log_path: Path, markers: tuple[str, ...], timeout: float, poll: float) -> tuple[bool, str, str]:
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        last = read_log(log_path)
        failure_marker = first_failure_marker(last)
        if failure_marker:
            return False, last, failure_marker
        if markers_in_order(last, markers):
            return True, last, ""
        time.sleep(poll)
    return False, last, ""


def wait_for_govc_markers(
    govc: str,
    remote_serial_log: str,
    local_log: Path,
    markers: tuple[str, ...],
    timeout: float,
    poll: float,
) -> tuple[bool, str, str]:
    deadline = time.monotonic() + timeout
    last = ""
    local_log.parent.mkdir(parents=True, exist_ok=True)
    while time.monotonic() < deadline:
        proc = run_command([govc, "datastore.download", remote_serial_log, str(local_log)])
        if proc.returncode != 0:
            last = proc.stderr + proc.stdout
        else:
            last = read_log(local_log)
            failure_marker = first_failure_marker(last)
            if failure_marker:
                return False, last, failure_marker
            if markers_in_order(last, markers):
                return True, last, ""
        time.sleep(poll)
    return False, last, ""


def vmrun_start(vmrun: str, vmx: Path, nogui: bool) -> int:
    mode = "nogui" if nogui else "gui"
    proc = run_command([vmrun, "start", str(vmx), mode])
    if proc.returncode != 0:
        print(proc.stdout, end="")
        print(proc.stderr, end="", file=sys.stderr)
        return 1
    return 0


def vmrun_stop(vmrun: str, vmx: Path) -> None:
    run_command([vmrun, "stop", str(vmx), "hard"])


def govc_power_on(govc: str, vm_name: str) -> int:
    proc = run_command([govc, "vm.power", "-on", vm_name])
    if proc.returncode != 0:
        print(proc.stdout, end="")
        print(proc.stderr, end="", file=sys.stderr)
        return 1
    return 0


def govc_power_off(govc: str, vm_name: str) -> None:
    run_command([govc, "vm.power", "-off", "-force", vm_name])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="CapyOS VMware+E1000 DHCP smoke harness")
    parser.add_argument("--provider", choices=("vmrun", "govc"), default="vmrun")
    parser.add_argument("--vmx", type=Path, help="Path to a vmrun-compatible .vmx template/VM")
    parser.add_argument("--vm-name", help="govc VM inventory name")
    parser.add_argument("--serial-log", type=Path, default=Path("build/ci/smoke_x64_vmware.serial.log"))
    parser.add_argument("--govc-serial-log", help="Datastore path used by govc datastore.download for the VM serial log")
    parser.add_argument("--summary-log", type=Path, default=Path("build/ci/smoke_x64_vmware.summary.log"))
    parser.add_argument("--iso", type=Path, default=Path("build/CapyOS-Installer-UEFI.iso"))
    parser.add_argument("--disk", type=Path, help="Optional provisioned VMware disk image to require before boot")
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--poll", type=float, default=2.0)
    parser.add_argument("--marker", action="append", dest="markers", help="Required marker in serial log; repeatable")
    parser.add_argument("--no-artifact-check", action="store_true")
    parser.add_argument("--no-poweroff", action="store_true")
    parser.add_argument("--gui", action="store_true", help="Use vmrun gui mode instead of nogui")
    parser.add_argument("--dry-run", action="store_true", help="Validate arguments and print the selected orchestration without powering on")
    parser.add_argument("--self-test", action="store_true")
    args, unknown = parser.parse_known_args()
    if "..." in unknown:
        parser.error("SMOKE_X64_VMWARE_ARGS contem placeholder '...'; informe argumentos reais de VMware")
    if unknown:
        parser.error("unrecognized arguments: " + " ".join(unknown))
    return args


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()
    repo_root = Path(__file__).resolve().parents[2]
    serial_log = (repo_root / args.serial_log).resolve() if not args.serial_log.is_absolute() else args.serial_log
    summary_log = (repo_root / args.summary_log).resolve() if not args.summary_log.is_absolute() else args.summary_log
    iso = (repo_root / args.iso).resolve() if args.iso and not args.iso.is_absolute() else args.iso
    disk = (repo_root / args.disk).resolve() if args.disk and not args.disk.is_absolute() else args.disk
    markers = unique_markers(tuple(args.markers) if args.markers else DEFAULT_MARKERS)
    if any(not marker for marker in markers):
        return fail("--marker vazio nao e permitido")

    if not args.no_artifact_check:
        rc = require_artifacts(repo_root, iso, disk)
        if rc != 0:
            return rc

    failure_marker = ""
    if args.provider == "vmrun":
        if not args.vmx:
            return fail("--vmx e obrigatorio para provider vmrun")
        vmrun = tool_path("vmrun")
        if not vmrun:
            return fail("vmrun nao encontrado no PATH")
        vmx = args.vmx.resolve()
        if not vmx.exists():
            return fail(f"VMX ausente: {vmx}")
        print(f"[info] provider=vmrun vmx={vmx} serial_log={serial_log}")
        if args.dry_run:
            return 0
        serial_log.parent.mkdir(parents=True, exist_ok=True)
        serial_log.write_text("", encoding="utf-8")
        try:
            rc = vmrun_start(vmrun, vmx, not args.gui)
            if rc != 0:
                return rc
            ok, log, failure_marker = wait_for_markers(serial_log, markers, args.timeout, args.poll)
        finally:
            if not args.no_poweroff:
                vmrun_stop(vmrun, vmx)
    else:
        if not args.vm_name:
            return fail("--vm-name e obrigatorio para provider govc")
        if not args.govc_serial_log:
            return fail("--govc-serial-log e obrigatorio para provider govc")
        govc = tool_path("govc")
        if not govc:
            return fail("govc nao encontrado no PATH")
        print(f"[info] provider=govc vm={args.vm_name} serial_log={args.govc_serial_log}")
        if args.dry_run:
            return 0
        try:
            rc = govc_power_on(govc, args.vm_name)
            if rc != 0:
                return rc
            ok, log, failure_marker = wait_for_govc_markers(govc, args.govc_serial_log, serial_log, markers, args.timeout, args.poll)
        finally:
            if not args.no_poweroff:
                govc_power_off(govc, args.vm_name)

    write_tail(summary_log, log)
    if failure_marker:
        print(f"[err] marker de falha encontrado no serial: {failure_marker}", file=sys.stderr)
        print(f"[err] tail gravado em {summary_log}", file=sys.stderr)
        return 1
    if not ok:
        print(f"[err] markers nao encontrados em ordem antes do timeout: {', '.join(markers)}", file=sys.stderr)
        print(f"[err] tail gravado em {summary_log}", file=sys.stderr)
        return 1
    print(f"[ok] VMware+E1000 DHCP smoke markers encontrados em ordem: {', '.join(markers)}")
    print(f"[ok] resumo gravado em {summary_log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
