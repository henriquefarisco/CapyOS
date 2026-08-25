from __future__ import annotations

import argparse
import ctypes
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import uuid
from ctypes import wintypes
from pathlib import Path

try:
    import msvcrt
except ModuleNotFoundError:  # Imported by the portable host contract on POSIX.
    msvcrt = None  # type: ignore[assignment]

from smoke_x64_auth import (
    complete_iso_install,
    installer_eligible_target_count,
    installer_select_target_by_size,
    login,
    maybe_run_first_boot_setup,
)
from smoke_x64_boot import smoke_first_boot, smoke_second_boot
from smoke_x64_helpers import ensure_shell_after_login
from smoke_x64_vmware_installer_contract import (
    RECOVERY_KEY_RE,
    exact_extent_size_mib,
    parse_flat_extent,
    render_evidence,
    render_scratch_vmx,
    sanitize_public_text,
    sha256_file,
)

SERIAL_CHAR_DELAY = 0.002


def parse_vmrun_list(text: str) -> tuple[Path, ...]:
    lines = tuple(line.strip() for line in text.splitlines() if line.strip())
    if not lines:
        raise RuntimeError("vmrun list returned no output")
    match = re.fullmatch(r"Total running VMs:\s*(\d+)", lines[0])
    if match is None:
        raise RuntimeError("vmrun list header is ambiguous")
    paths = tuple(Path(line) for line in lines[1:])
    if len(paths) != int(match.group(1), 10):
        raise RuntimeError("vmrun list count does not match its paths")
    return paths


def same_windows_path(left: Path, right: Path) -> bool:
    return os.path.normcase(os.path.abspath(left)) == os.path.normcase(
        os.path.abspath(right)
    )


class PhaseProcess:
    def __init__(self, console: "VmwareConsole") -> None:
        self.console = console
        self.returncode = 0

    def poll(self) -> int | None:
        text = self.console.text()
        if self.console.ended.is_set() or any(
            marker in text
            for marker in (
                "Installation complete. Rebooting...",
                "Initial setup complete. Rebooting",
                "Configuracao inicial concluida. Reiniciando",
                "Configuracion inicial completa. Reiniciando",
                "Rebooting...",
                "Reiniciando...",
                "Powering off...",
                "Desligando...",
                "Apagando...",
            )
        ):
            return self.returncode
        return None


class VmwareConsole:
    def __init__(
        self,
        vmrun: Path,
        vmx: Path,
        pipe_name: str,
        log_path: Path,
        *,
        secrets: tuple[str, ...] = (),
        verbose: bool = False,
    ) -> None:
        self.vmrun = vmrun
        self.vmx = vmx
        self.pipe_path = "\\\\.\\pipe\\" + pipe_name
        self.log_path = log_path
        self.secrets = secrets
        self.verbose = verbose
        self.proc = PhaseProcess(self)
        self.ended = threading.Event()
        self._lock = threading.Lock()
        self._buffer = bytearray()
        self._pipe: int | None = None
        self._reader: threading.Thread | None = None

    def start(self) -> None:
        command = [str(self.vmrun), "-T", "ws", "start", str(self.vmx), "nogui"]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        self.write_vmrun_log("start", result)
        if result.returncode != 0:
            start_error = result.stderr.strip() or result.stdout.strip() or "vmrun start failed"
            try:
                self.stop()
            except BaseException as cleanup_error:
                raise RuntimeError(
                    f"{start_error}; cleanup failed: {cleanup_error}"
                ) from cleanup_error
            raise RuntimeError(start_error)
        try:
            self._pipe = self._open_pipe(30.0)
            self._reader = threading.Thread(target=self._read_pipe, daemon=True)
            self._reader.start()
        except BaseException:
            self.stop()
            raise

    def stop(self) -> None:
        # Persist the serial tail before asking VMware to stop.  In particular,
        # this preserves diagnostics if vmrun itself stalls during teardown.
        self.write_public_log()
        result = subprocess.run(
            [str(self.vmrun), "-T", "ws", "stop", str(self.vmx), "hard"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.write_vmrun_log("stop", result)
        list_result = subprocess.run(
            [str(self.vmrun), "-T", "ws", "list"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.write_vmrun_log("list-after-stop", list_result)
        self.ended.set()
        if self._pipe is not None:
            try:
                os.close(self._pipe)
            except OSError:
                pass
            self._pipe = None
        if self._reader is not None:
            self._reader.join(timeout=2.0)
        self.write_public_log()
        if list_result.returncode != 0:
            raise RuntimeError("vmrun list failed after stopping disposable VM")
        running = parse_vmrun_list(list_result.stdout)
        if any(same_windows_path(path, self.vmx) for path in running):
            raise RuntimeError("disposable VMware VM remained running after hard stop")

    def write_public_log(self) -> None:
        text = sanitize_public_text(self.text(), self.secrets)
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.log_path.with_suffix(self.log_path.suffix + ".tmp")
        temporary.write_text(text, encoding="utf-8")
        os.replace(temporary, self.log_path)

    def write_vmrun_log(self, action: str, result: subprocess.CompletedProcess[str]) -> None:
        path = self.log_path.with_suffix(".vmrun.log")
        output = sanitize_public_text(
            f"action={action}\nexit_code={result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}\n",
            self.secrets,
        )
        with path.open("a", encoding="utf-8") as stream:
            stream.write(output)

    def _open_pipe(self, timeout: float):
        if os.name != "nt" or msvcrt is None:
            raise RuntimeError("VMware installer named-pipe console requires Windows Python")
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.WaitNamedPipeW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD)
        kernel32.WaitNamedPipeW.restype = wintypes.BOOL
        kernel32.CreateFileW.argtypes = (
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.HANDLE,
        )
        kernel32.CreateFileW.restype = wintypes.HANDLE
        deadline = time.monotonic() + timeout
        invalid_handle = ctypes.c_void_p(-1).value
        while time.monotonic() < deadline:
            if kernel32.WaitNamedPipeW(self.pipe_path, 1000):
                handle = kernel32.CreateFileW(
                    self.pipe_path, 0xC0000000, 0, None, 3, 0, None
                )
                if handle != invalid_handle:
                    return msvcrt.open_osfhandle(
                        int(handle), os.O_RDWR | os.O_BINARY
                    )
            time.sleep(0.1)
        raise TimeoutError(f"timeout opening VMware serial pipe {self.pipe_path}")

    def _read_pipe(self) -> None:
        while not self.ended.is_set() and self._pipe is not None:
            try:
                available = self._pipe_bytes_available()
            except OSError:
                self.ended.set()
                return
            if available == 0:
                time.sleep(0.01)
                continue
            try:
                data = os.read(self._pipe, min(4096, available))
            except OSError:
                self.ended.set()
                return
            if not data:
                self.ended.set()
                return
            with self._lock:
                self._buffer.extend(data)
            if self.verbose:
                sys.stdout.write(sanitize_public_text(data.decode("latin-1", errors="replace"), self.secrets))
                sys.stdout.flush()

    def _pipe_bytes_available(self) -> int:
        """Return queued bytes without issuing a blocking synchronous read.

        Windows serial named pipes are full duplex, but a pending synchronous
        ReadFile on a handle serializes a WriteFile made from another thread on
        that same handle.  Peeking first keeps the reader idle when the guest is
        waiting for input, so send_text() can write without deadlocking behind
        the reader.
        """
        if self._pipe is None:
            return 0
        if msvcrt is None:
            raise RuntimeError("VMware named-pipe polling requires Windows Python")
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.PeekNamedPipe.argtypes = (
            wintypes.HANDLE,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.LPVOID,
            ctypes.POINTER(wintypes.DWORD),
            wintypes.LPVOID,
        )
        kernel32.PeekNamedPipe.restype = wintypes.BOOL
        available = wintypes.DWORD(0)
        handle = wintypes.HANDLE(msvcrt.get_osfhandle(self._pipe))
        if not kernel32.PeekNamedPipe(
            handle, None, 0, None, ctypes.byref(available), None
        ):
            error = ctypes.get_last_error()
            if error in (109, 232, 233):
                self.ended.set()
                return 0
            raise ctypes.WinError(error)
        return int(available.value)

    def marker(self) -> int:
        with self._lock:
            return len(self._buffer)

    def text(self) -> str:
        with self._lock:
            return self._buffer.decode("latin-1", errors="replace")

    def tail(self, max_bytes: int = 3500) -> str:
        with self._lock:
            data = self._buffer[-max_bytes:]
        return data.decode("latin-1", errors="replace")

    def text_since(self, start_at: int) -> str:
        return self.text()[start_at:]

    def send_byte(self, value: int) -> None:
        if self._pipe is None or not 0 <= value <= 255:
            raise RuntimeError("VMware serial pipe is unavailable")
        os.write(self._pipe, bytes([value]))

    def send_text(self, text: str, newline: bool = False) -> None:
        if self._pipe is None:
            raise RuntimeError("VMware serial pipe is unavailable")
        payload = text.encode("ascii", errors="ignore") + (b"\r" if newline else b"")
        for index, value in enumerate(payload):
            os.write(self._pipe, bytes([value]))
            if index + 1 < len(payload):
                time.sleep(SERIAL_CHAR_DELAY)

    def send_line(self, text: str) -> None:
        self.send_text(text, newline=True)

    def send_firmware_line(self, text: str) -> None:
        if self._pipe is None:
            raise RuntimeError("VMware serial pipe is unavailable")
        payload = text.encode("ascii", errors="ignore") + b"\n"
        for index, value in enumerate(payload):
            os.write(self._pipe, bytes([value]))
            if index + 1 < len(payload):
                time.sleep(SERIAL_CHAR_DELAY)

    def wait_for(
        self,
        pattern: str,
        timeout: float,
        start_at: int = 0,
        ignore_line_breaks: bool = False,
    ) -> None:
        def contains() -> bool:
            text = self.text()[start_at:]
            expected = pattern
            if ignore_line_breaks:
                text = text.replace("\r", "").replace("\n", "")
                expected = expected.replace("\r", "").replace("\n", "")
            return expected in text

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if contains():
                return
            if self.ended.is_set():
                raise RuntimeError(f"VMware phase ended before pattern {pattern!r}")
            time.sleep(0.05)
        if contains():
            return
        raise TimeoutError(f"timeout waiting for pattern: {pattern!r}")

    def wait_for_any(self, patterns: list[str], timeout: float, start_at: int = 0) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.text()[start_at:]
            for pattern in patterns:
                if pattern in text:
                    return pattern
            if self.ended.is_set():
                raise RuntimeError(f"VMware phase ended before patterns {patterns!r}")
            time.sleep(0.05)
        text = self.text()[start_at:]
        for pattern in patterns:
            if pattern in text:
                return pattern
        raise TimeoutError(f"timeout waiting for patterns: {patterns!r}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="CapyOS VMware installer wizard gate")
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--vmrun", type=Path, default=Path(r"C:\Program Files\VMware\VMware Workstation\vmrun.exe"))
    parser.add_argument("--vdiskmanager", type=Path, default=Path(r"C:\Program Files\VMware\VMware Workstation\vmware-vdiskmanager.exe"))
    parser.add_argument("--work-root", type=Path, default=Path("build/ci/vmware-installer"))
    parser.add_argument("--evidence", type=Path, default=Path("build/ci/installer-wizard-evidence.manifest"))
    parser.add_argument("--target-size", default="2GB")
    parser.add_argument("--guard-size", default="3GB")
    parser.add_argument("--step-timeout", type=float, default=180.0)
    parser.add_argument("--user", default="admin")
    parser.add_argument("--password", default="vmware-installer-pass")
    parser.add_argument("--keep-vm", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def run_checked(command: list[str]) -> None:
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "command failed")


def current_release_tag(repo_root: Path) -> str:
    text = (repo_root / "include/core/version.h").read_text(encoding="utf-8")
    match = re.search(
        r'^\s*#define\s+CAPYOS_VERSION_FULL\s+"([^"]+)"\s*$',
        text,
        flags=re.MULTILINE,
    )
    if not match:
        raise ValueError("CAPYOS_VERSION_FULL is missing")
    return match.group(1)


def create_vmdk(vdiskmanager: Path, descriptor: Path, size: str) -> Path:
    if descriptor.exists():
        raise FileExistsError(descriptor)
    run_checked([str(vdiskmanager), "-c", "-s", size, "-a", "lsilogic", "-t", "2", str(descriptor)])
    return parse_flat_extent(descriptor)


def write_vmx(path: Path, text: str) -> None:
    temporary = path.with_suffix(".vmx.tmp")
    temporary.write_text(text, encoding="utf-8")
    os.replace(temporary, path)


def extract_recovery_key(text: str) -> str:
    matches = tuple(dict.fromkeys(match.group(0) for match in RECOVERY_KEY_RE.finditer(text)))
    if len(matches) != 1:
        raise RuntimeError(f"expected exactly one recovery key, found {len(matches)}")
    return matches[0]


def start_console(
    vmrun: Path,
    vmx: Path,
    pipe_name: str,
    log_path: Path,
    *,
    secrets: tuple[str, ...] = (),
    verbose: bool = False,
) -> VmwareConsole:
    console = VmwareConsole(vmrun, vmx, pipe_name, log_path, secrets=secrets, verbose=verbose)
    console.start()
    return console


def reset_evidence_output(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.unlink(missing_ok=True)
    path.with_suffix(path.suffix + ".tmp").unlink(missing_ok=True)


def main() -> int:
    args = parse_args()
    if os.name != "nt":
        print("[err] VMware installer gate must run with Windows Python", file=sys.stderr)
        return 2
    repo_root = Path(__file__).resolve().parents[2]
    iso = args.iso.resolve()
    vmrun = args.vmrun.resolve()
    vdiskmanager = args.vdiskmanager.resolve()
    work_root = (repo_root / args.work_root).resolve() if not args.work_root.is_absolute() else args.work_root.resolve()
    evidence_path = (repo_root / args.evidence).resolve() if not args.evidence.is_absolute() else args.evidence.resolve()
    safe_root = (repo_root / "build/ci").resolve()
    for path in (work_root, evidence_path):
        try:
            path.relative_to(safe_root)
        except ValueError:
            print(f"[err] VMware gate path must stay under {safe_root}: {path}", file=sys.stderr)
            return 2
    if not iso.is_file() or not vmrun.is_file() or not vdiskmanager.is_file():
        print("[err] ISO or VMware executable is missing", file=sys.stderr)
        return 2
    reset_evidence_output(evidence_path)
    artifact_root = evidence_path.parent
    run_id = uuid.uuid4().hex[:12]
    run_root = work_root / run_id
    try:
        run_root.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        print("[err] VMware scratch run directory already exists", file=sys.stderr)
        return 2
    pipe_name = f"capyos-installer-{run_id}"
    target_descriptor = run_root / "target.vmdk"
    guard_descriptor = run_root / "guard.vmdk"
    vmx = run_root / "CapyOS-Installer.vmx"
    installer_log = artifact_root / f"smoke_x64_vmware_installer_{run_id}.installer.log"
    boot1_log = artifact_root / f"smoke_x64_vmware_installer_{run_id}.boot1.log"
    marker_log = artifact_root / f"smoke_x64_vmware_installer_{run_id}.marker-write.log"
    boot2_log = artifact_root / f"smoke_x64_vmware_installer_{run_id}.boot2.log"
    success = False
    recovery_key = ""
    try:
        iso_sha256_before = sha256_file(iso)
        target_extent = create_vmdk(vdiskmanager, target_descriptor, args.target_size)
        guard_extent = create_vmdk(vdiskmanager, guard_descriptor, args.guard_size)
        target_size_mib = exact_extent_size_mib(target_extent)
        guard_size_mib = exact_extent_size_mib(guard_extent)
        if target_extent == guard_extent or iso in (target_extent, guard_extent):
            raise RuntimeError("ISO, target and guard must be distinct files")
        if guard_size_mib <= target_size_mib:
            raise RuntimeError("guard disk must be larger than the explicit target")
        with guard_extent.open("r+b") as stream:
            stream.write(b"CAPYOS-VMWARE-GUARD-BEGIN-v1")
            stream.seek(-4096, os.SEEK_END)
            stream.write(b"CAPYOS-VMWARE-GUARD-END-v1")
        target_before = sha256_file(target_extent)
        guard_before = sha256_file(guard_extent)
        vmx_text = render_scratch_vmx(
            display_name=f"CapyOS Installer {run_id}", iso_path=iso,
            target_descriptor=target_descriptor, guard_descriptor=guard_descriptor,
            pipe_name=pipe_name, boot_from="cdrom",
        )
        write_vmx(vmx, vmx_text)
        installer = start_console(vmrun, vmx, pipe_name, installer_log, verbose=False)
        installer_error: BaseException | None = None
        try:
            complete_iso_install(
                installer, args.step_timeout, "us", args.user, args.password,
                expected_eligible_targets=2, target_selection=1,
                target_size_mib=target_size_mib,
            )
        except BaseException as exc:
            installer_error = exc
        finally:
            raw_installer = installer.text()
            try:
                if "=== Volume Recovery Key ===" in raw_installer:
                    recovery_key = extract_recovery_key(raw_installer)
                    installer.secrets = (recovery_key,)
                elif installer_error is None:
                    installer_error = RuntimeError(
                        "installer did not publish exactly one recovery key"
                    )
            finally:
                installer.stop()
        guard_after_install = sha256_file(guard_extent)
        target_after_install = sha256_file(target_extent)
        if guard_after_install != guard_before:
            raise RuntimeError("VMware guard disk changed during installer") from installer_error
        if target_after_install == target_before:
            detail = (
                f": {type(installer_error).__name__}: {installer_error}"
                if installer_error is not None else ""
            )
            raise RuntimeError(
                "VMware target disk did not change during installer" + detail
            ) from installer_error
        if installer_error is not None:
            raise installer_error
        eligible_count = installer_eligible_target_count(raw_installer)
        if eligible_count != 2:
            raise RuntimeError("VMware installer did not expose exactly two eligible targets")
        selected_index, selected_path_id = installer_select_target_by_size(
            raw_installer, target_size_mib
        )
        if selected_index != 1:
            raise RuntimeError("VMware target VMDK was not the first eligible disk")
        write_vmx(vmx, render_scratch_vmx(
            display_name=f"CapyOS Installer {run_id}", iso_path=iso,
            target_descriptor=target_descriptor, guard_descriptor=guard_descriptor,
            pipe_name=pipe_name, boot_from="hdd",
        ))
        boot1 = start_console(vmrun, vmx, pipe_name, boot1_log, secrets=(recovery_key,), verbose=args.verbose)
        marker_written = False
        marker_session_used = False
        try:
            setup_result = maybe_run_first_boot_setup(
                boot1, args.step_timeout, args.user, args.password, "us",
                volume_key=recovery_key, module_profile="basic",
                require_interactive=True,
            )
            if setup_result != "rebooted":
                mode = login(boot1, args.step_timeout, args.user, args.password, allow_desktop=True)
                ensure_shell_after_login(boot1, args.step_timeout, mode)
                smoke_first_boot(boot1, args.step_timeout, args.user, args.password, "persist-ok")
                marker_written = True
        finally:
            boot1.stop()
        if not marker_written:
            marker_session_used = True
            marker_session = start_console(vmrun, vmx, pipe_name, marker_log, secrets=(recovery_key,), verbose=args.verbose)
            try:
                setup_result = maybe_run_first_boot_setup(
                    marker_session, args.step_timeout, args.user, args.password, "us",
                    volume_key=recovery_key, module_profile="basic",
                    require_interactive=False,
                )
                if setup_result == "rebooted":
                    raise RuntimeError("VMware first boot rebooted twice before persistence marker")
                mode = login(marker_session, args.step_timeout, args.user, args.password, allow_desktop=True)
                ensure_shell_after_login(marker_session, args.step_timeout, mode)
                smoke_first_boot(marker_session, args.step_timeout, args.user, args.password, "persist-ok")
            finally:
                marker_session.stop()
        if not marker_log.exists():
            marker_log.write_bytes(b"")
        boot2 = start_console(vmrun, vmx, pipe_name, boot2_log, secrets=(recovery_key,), verbose=args.verbose)
        try:
            mode = login(boot2, args.step_timeout, args.user, args.password, allow_desktop=True)
            ensure_shell_after_login(boot2, args.step_timeout, mode)
            smoke_second_boot(
                boot2,
                args.step_timeout,
                args.user,
                args.password,
                "persist-ok",
                expected_gateway=None,
            )
        finally:
            boot2.stop()
        guard_after_final = sha256_file(guard_extent)
        if guard_after_final != guard_before:
            raise RuntimeError("VMware guard disk changed after installed boots")
        iso_sha256_after = sha256_file(iso)
        if iso_sha256_after != iso_sha256_before:
            raise RuntimeError("installer ISO changed during the VMware wizard gate")
        public_logs = (installer_log, boot1_log, marker_log, boot2_log)
        if not all(path.is_file() for path in public_logs):
            raise RuntimeError("VMware installer public logs are incomplete")
        fields = {
            "format": "capyos-installer-wizard-evidence-manifest-v3",
            "release_tag": current_release_tag(repo_root),
            "track": "UEFI/GPT/x86_64",
            "provider": "vmware-workstation",
            "firmware": "uefi",
            "secure_boot": "disabled",
            "vcpu_count": "2",
            "memory_mib": "1024",
            "network": "nat-e1000",
            "iso_artifact": iso.name,
            "iso_sha256": iso_sha256_before,
            "iso_sha256_before": iso_sha256_before,
            "iso_sha256_after": iso_sha256_after,
            "iso_unchanged": "yes",
            "eligible_target_count": str(eligible_count),
            "target_selected_explicitly": "yes",
            "target_selected_index": str(selected_index),
            "target_path_id": selected_path_id,
            "target_size_mib": str(target_size_mib),
            "guard_size_mib": str(guard_size_mib),
            "target_identity_revalidated": "yes",
            "erase_token_confirmed": "yes",
            "target_sha256_before": target_before,
            "target_sha256_after": target_after_install,
            "target_changed": "yes",
            "guard_sha256_before": guard_before,
            "guard_sha256_after": guard_after_final,
            "guard_unchanged": "yes",
            "fresh_install_completed": "yes",
            "first_boot_completed": "yes",
            "login_completed": "yes",
            "persistence_marker_written": "yes",
            "persistence_marker_read_after_reboot": "yes",
            "recovery_key_redacted": "yes",
            "recovery_key_included": "no",
            "marker_session_used": "yes" if marker_session_used else "no",
            "installer_log": installer_log.name,
            "installer_log_sha256": sha256_file(installer_log),
            "boot1_log": boot1_log.name,
            "boot1_log_sha256": sha256_file(boot1_log),
            "marker_log": marker_log.name,
            "marker_log_sha256": sha256_file(marker_log),
            "boot2_log": boot2_log.name,
            "boot2_log_sha256": sha256_file(boot2_log),
        }
        evidence_text = render_evidence(fields)
        temporary = evidence_path.with_suffix(evidence_path.suffix + ".tmp")
        temporary.write_text(evidence_text, encoding="utf-8")
        os.replace(temporary, evidence_path)
        success = True
        print(f"[ok] VMware installer wizard gate passed: {evidence_path}")
        return 0
    except BaseException as exc:
        print(f"[err] VMware installer wizard gate failed: {type(exc).__name__}: {exc}", file=sys.stderr)
        print(f"[info] preserving VMware scratch evidence: {run_root}", file=sys.stderr)
        return 1
    finally:
        if recovery_key:
            recovery_key = ""
        if success and not args.keep_vm:
            shutil.rmtree(run_root, ignore_errors=False)


if __name__ == "__main__":
    raise SystemExit(main())
