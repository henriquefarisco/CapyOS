#!/usr/bin/env python3

from io import BytesIO
import os
from pathlib import Path
from tempfile import TemporaryDirectory

from smoke_x64_auth import (
    complete_iso_install,
    installer_eligible_target_count,
    installer_eligible_targets,
    installer_has_single_eligible_target,
    installer_select_target_by_size,
    module_install_completed,
    require_first_boot_wizard,
    require_installer_target_count,
)
from smoke_x64_common import cleanup_file, create_runtime_ovmf_vars
from smoke_x64_helpers import ensure_shell_after_login
from smoke_x64_iso_install import (
    extract_volume_key,
    file_sha256,
    installer_failed_before_loader,
    prepare_exclusive_disk,
    prepare_guard_disk,
    require_safe_disk_path,
)
from smoke_x64_qemu_installer_no_uart import (
    SOURCE_COM1_MARKER,
    SOURCE_CONIN_MARKER,
    TARGET_CANCEL_MARKER,
    TARGET_PROMPT_MARKER,
    chars_to_sendkey_lines as no_uart_sendkey_lines,
    installer_input_trace_passes,
    make_no_uart_qemu_cmd,
    qtree_proves_no_isa_serial,
)
from smoke_x64_session import (
    SmokeSession,
    make_qemu_cmd,
    reset_capture_file,
    text_contains_pattern,
)
import smoke_x64_vmware_installer as vmware_installer
from smoke_x64_vmware_installer import VmwareConsole, parse_vmrun_list
from smoke_x64_vmware_installer import reset_evidence_output
from smoke_x64_vmware_installer_contract import (
    contains_recovery_key,
    exact_extent_size_mib,
    parse_evidence,
    parse_flat_extent,
    render_evidence,
    render_scratch_vmx,
    sanitize_public_text,
)


class LegacyInstallerSession:
    def marker(self) -> int:
        return 0

    def wait_for_any(
        self, patterns: list[str], timeout: float, start_at: int = 0
    ) -> str:
        del patterns, timeout, start_at
        return "Press 'I' to start"


class FakeDesktopSession:
    def __init__(self) -> None:
        self.sent: list[str | int] = []
        self.waited: list[str] = []

    def marker(self) -> int:
        return 0

    def send_line(self, text: str) -> None:
        self.sent.append(text)

    def send_byte(self, value: int) -> None:
        self.sent.append(value)

    def wait_for(self, text: str, timeout: float, start_at: int = 0) -> None:
        del timeout, start_at
        self.waited.append(text)


def main() -> int:
    with TemporaryDirectory() as temp:
        stale_capture = Path(temp) / "stale.debugcon.log"
        stale_capture.write_text("old-login-evidence", encoding="utf-8")
        reset_capture_file(stale_capture)
        if stale_capture.read_bytes() != b"":
            print("[FAIL] stale debugcon evidence survived session reset")
            return 1
        firmware_log = Path(temp) / "firmware.log"
        firmware_log.write_text(
            "Start HTTP Boot over IPv4\nEFI Internal Shell\n", encoding="utf-8"
        )
        if not installer_failed_before_loader(firmware_log):
            print("[FAIL] pre-loader firmware miss is not retriable")
            return 1
        firmware_log.write_text(
            "CapyOS UEFI loader: iniciando\nEFI Internal Shell\n", encoding="utf-8"
        )
        if installer_failed_before_loader(firmware_log):
            print("[FAIL] post-loader installer failure became retriable")
            return 1
    if not text_contains_pattern(
        "remote=https://github.com/releases/latest/downlo\nad/latest.ini",
        "/download/latest.ini",
        ignore_line_breaks=True,
    ):
        print("[FAIL] wrapped update endpoint was not recognized")
        return 1
    if text_contains_pattern(
        "remote=https://github.com/releases/latest/downlo\nad/latest.ini",
        "/download/latest.ini",
    ):
        print("[FAIL] strict matching unexpectedly ignored line breaks")
        return 1
    cases = {
        "[installer] eligible-targets=1\n": True,
        "prefix\n[installer] eligible-targets=1\r\nnext\n": True,
        "[installer] eligible-targets=0\n": False,
        "[installer] eligible-targets=2\n": False,
        "[installer] eligible-targets=10\n": False,
        "[installer] eligible-targets=16\n": False,
        "noise eligible-targets=1\n": False,
    }
    for text, expected in cases.items():
        actual = installer_has_single_eligible_target(text)
        if actual != expected:
            print(
                "[FAIL] installer smoke contract:",
                repr(text),
                "got",
                actual,
                "expected",
                expected,
            )
            return 1
    if installer_eligible_target_count("[installer] eligible-targets=2\n") != 2:
        print("[FAIL] multi-disk eligible target count was not parsed")
        return 1
    if installer_eligible_target_count(
        "[installer] eligible-targets=2\n[installer] eligible-targets=2\n"
    ) != 2:
        print("[FAIL] identical mirrored target evidence was rejected")
        return 1
    for text in (
        "[installer] eligible-targets=x\n",
        "[installer] eligible-targets=1\n[installer] eligible-targets=2\n",
        "noise eligible-targets=2\n",
    ):
        if installer_eligible_target_count(text) is not None:
            print("[FAIL] malformed/stale eligible target evidence accepted:", repr(text))
            return 1
    require_installer_target_count("[installer] eligible-targets=2\n", 2)
    if not module_install_completed("Install complete. Reboot to activate."):
        print("[FAIL] current module completion marker was rejected")
        return 1
    if module_install_completed("[modules] partial install"):
        print("[FAIL] partial module installation was accepted")
        return 1
    try:
        complete_iso_install(LegacyInstallerSession(), 1.0, "us")
    except RuntimeError:
        pass
    else:
        print("[FAIL] installer without explicit target selection was accepted")
        return 1
    candidate_text = (
        "  [1] PathId 0123456789ABCDEF, MediaId 3 - 2048 MiB - eligible (1794 MiB DATA)\n"
        "  [2] PathId FEDCBA9876543210, MediaId 4 - 3072 MiB - eligible (2818 MiB DATA)\n"
    )
    if installer_eligible_targets(candidate_text) != (
        (1, "0123456789abcdef", 2048),
        (2, "fedcba9876543210", 3072),
    ):
        print("[FAIL] installer candidate table was not parsed")
        return 1
    if installer_select_target_by_size(candidate_text, 2048) != (
        1,
        "0123456789abcdef",
    ):
        print("[FAIL] installer target was not selected by capacity")
        return 1
    try:
        installer_select_target_by_size(candidate_text + candidate_text, 2048)
    except RuntimeError:
        pass
    else:
        print("[FAIL] ambiguous installer target capacity was accepted")
        return 1
    try:
        require_installer_target_count("[installer] eligible-targets=1\n", 2)
    except RuntimeError:
        pass
    else:
        print("[FAIL] wrong eligible target count accepted")
        return 1
    require_first_boot_wizard("Available keyboard layouts:", True)
    require_first_boot_wizard("User:", False)
    for marker in ("User:", "Usuario:", "Provisionamento automatico"):
        try:
            require_first_boot_wizard(marker, True)
        except RuntimeError:
            continue
        print("[FAIL] fresh-install smoke accepted direct login:", marker)
        return 1
    fake = FakeDesktopSession()
    if ensure_shell_after_login(fake, 1.0, "desktop") != "shell":
        print("[FAIL] desktop login was not converted to shell evidence")
        return 1
    if fake.sent != [0x9D] or fake.waited != ["[desktop] session stopped", ">~> "]:
        print("[FAIL] desktop-to-shell transition contract changed")
        return 1
    channel_session = SmokeSession([], 0, Path("unused.log"))
    channel_session._logf = BytesIO()
    if not channel_session._record_debugcon_data(b"early-debug\n"):
        print("[FAIL] debugcon channel rejected diagnostic data")
        return 1
    channel_session._buf.extend(b"serial-output\n")
    if channel_session.text() != "serial-output\n\nearly-debug\n":
        print("[FAIL] debugcon/COM1 channels were interleaved")
        return 1
    marker = channel_session.marker()
    channel_session._buf.extend(b"Current theme: capyos\n")
    channel_session._debugcon_buf.extend(b"Current theme: capyos\n")
    if "Current theme: capyos" not in channel_session.text_since(marker):
        print("[FAIL] channel-aware marker hid complete mirrored output")
        return 1
    pipe_console = VmwareConsole(
        Path("vmrun"), Path("scratch.vmx"), "test-pipe", Path("unused.log")
    )
    pipe_console._pipe = 123
    queued = iter((0, 3))
    read_calls: list[tuple[int, int]] = []

    def fake_available() -> int:
        try:
            return next(queued)
        except StopIteration:
            pipe_console.ended.set()
            return 0

    original_read = vmware_installer.os.read
    pipe_console._pipe_bytes_available = fake_available  # type: ignore[method-assign]
    vmware_installer.os.read = lambda fd, size: (
        read_calls.append((fd, size)) or b"abc"
    )
    try:
        pipe_console._read_pipe()
    finally:
        vmware_installer.os.read = original_read
    if read_calls != [(123, 3)] or pipe_console.text() != "abc":
        print("[FAIL] VMware pipe reader performed a blocking empty read")
        return 1
    if len(parse_vmrun_list("Total running VMs: 1\nC:\\scratch\\one.vmx\n")) != 1:
        print("[FAIL] VMware running-VM list lost its exact path")
        return 1
    try:
        parse_vmrun_list("Total running VMs: 1\n")
    except RuntimeError:
        pass
    else:
        print("[FAIL] ambiguous VMware running-VM list was accepted")
        return 1
    target = Path("target.img")
    guard = Path("guard.img")
    sata_cmd = make_qemu_cmd(
        "qemu-system-x86_64", "OVMF_CODE.fd", Path("OVMF_VARS.fd"),
        target, 12345, 1024, extra_disks=(guard,)
    )
    sata_text = " ".join(str(part) for part in sata_cmd)
    if "server,nowait" not in sata_text or "wait=on" in sata_text:
        print("[FAIL] QEMU serial capture can block firmware disk enumeration")
        return 1
    if "once=c,menu=off" not in sata_text:
        print("[FAIL] installed-disk boot is not pinned with BootNext")
        return 1
    if "file=target.img" not in sata_text or "file=guard.img" not in sata_text:
        print("[FAIL] SATA multi-disk command omitted target or guard")
        return 1
    if sata_text.index("file=target.img") > sata_text.index("file=guard.img"):
        print("[FAIL] guard disk precedes explicit install target")
        return 1
    previous_accelerator = os.environ.get("CAPYOS_QEMU_ACCEL")
    try:
        os.environ["CAPYOS_QEMU_ACCEL"] = "tcg"
        no_uart_cmd = make_no_uart_qemu_cmd(
            qemu_bin="qemu-system-x86_64",
            ovmf_code="OVMF_CODE.fd",
            ovmf_vars_runtime=Path("OVMF_VARS.fd"),
            iso_path=Path("installer.iso"),
            disk_path=target,
            memory_mb=1024,
            debugcon_log=Path("no-uart.debugcon.log"),
            monitor_socket=Path("hmp.sock"),
        )
        serial_options = [
            index for index, value in enumerate(no_uart_cmd) if value == "-serial"
        ]
        if len(serial_options) != 1 or no_uart_cmd[serial_options[0] + 1] != "none":
            print("[FAIL] no-UART QEMU command did not disable serial exactly once")
            return 1
        no_uart_text = " ".join(str(part) for part in no_uart_cmd)
        if "tcp:" in no_uart_text or "once=d,menu=off" not in no_uart_text:
            print("[FAIL] no-UART smoke retained serial TCP or lost ISO boot")
            return 1
        if "unix:hmp.sock,server,nowait" not in no_uart_text:
            print("[FAIL] no-UART smoke lacks its HMP input channel")
            return 1
        if "isa-debugcon.iobase=0xe9" not in no_uart_text:
            print("[FAIL] no-UART smoke lacks independent debugcon evidence")
            return 1
        if "-nic none" not in no_uart_text:
            print("[FAIL] no-UART smoke retained unrelated network hardware")
            return 1
        if "file=installer.iso,media=cdrom,readonly=on" not in no_uart_text:
            print("[FAIL] no-UART smoke omitted the read-only installer ISO")
            return 1
        if no_uart_sendkey_lines("0\n") != ["sendkey 0\n", "sendkey ret\n"]:
            print("[FAIL] no-UART smoke does not inject exact cancel input")
            return 1
        try:
            no_uart_sendkey_lines("1")
        except ValueError:
            pass
        else:
            print("[FAIL] no-UART smoke accepted a destructive selection key")
            return 1
        valid_qtree = (
            "info qtree\n"
            "bus: main-system-bus\n"
            "  type System\n"
            "  dev: i8042\n"
            "    dev: ps2-kbd\n"
            "(qemu)"
        )
        if not qtree_proves_no_isa_serial(valid_qtree):
            print("[FAIL] no-UART smoke rejected valid serial-free qtree")
            return 1
        invalid_qtrees = (
            "",
            "unknown command: 'info qtree'\n(qemu)",
            "bus: main-system-bus\n  type System\n",
            valid_qtree.replace("dev: i8042", "dev: isa-serial"),
        )
        if any(qtree_proves_no_isa_serial(text) for text in invalid_qtrees):
            print("[FAIL] no-UART smoke accepted inconclusive/serial qtree")
            return 1
        valid_trace = (
            f"boot\n{TARGET_PROMPT_MARKER}\n{SOURCE_CONIN_MARKER}\n"
            f"{TARGET_CANCEL_MARKER}\n"
        )
        if not installer_input_trace_passes(valid_trace):
            print("[FAIL] no-UART smoke rejected its valid debugcon trace")
            return 1
        invalid_traces = (
            TARGET_PROMPT_MARKER,
            TARGET_CANCEL_MARKER,
            f"{TARGET_PROMPT_MARKER}\n{TARGET_CANCEL_MARKER}",
            f"{TARGET_PROMPT_MARKER}\n{SOURCE_COM1_MARKER}\n"
            f"{SOURCE_CONIN_MARKER}\n{TARGET_CANCEL_MARKER}",
            f"{TARGET_PROMPT_MARKER}\n{TARGET_PROMPT_MARKER}\n"
            f"{SOURCE_CONIN_MARKER}\n{TARGET_CANCEL_MARKER}",
            f"{TARGET_CANCEL_MARKER}\n{SOURCE_CONIN_MARKER}\n"
            f"{TARGET_PROMPT_MARKER}",
        )
        if any(installer_input_trace_passes(text) for text in invalid_traces):
            print("[FAIL] no-UART smoke accepted an incomplete/repeated trace")
            return 1
        os.environ["CAPYOS_QEMU_ACCEL"] = "kvm"
        accelerated = make_qemu_cmd(
            "qemu-system-x86_64", "OVMF_CODE.fd", Path("OVMF_VARS.fd"),
            target, 12345, 1024
        )
        if "q35,accel=kvm" not in accelerated:
            print("[FAIL] explicit KVM accelerator was not honored")
            return 1
        os.environ["CAPYOS_QEMU_ACCEL"] = "invalid"
        try:
            make_qemu_cmd(
                "qemu-system-x86_64", "OVMF_CODE.fd", Path("OVMF_VARS.fd"),
                target, 12345, 1024
            )
        except ValueError:
            pass
        else:
            print("[FAIL] invalid QEMU accelerator was accepted")
            return 1
    finally:
        if previous_accelerator is None:
            os.environ.pop("CAPYOS_QEMU_ACCEL", None)
        else:
            os.environ["CAPYOS_QEMU_ACCEL"] = previous_accelerator
    cdrom_cmd = make_qemu_cmd(
        "qemu-system-x86_64", "OVMF_CODE.fd", Path("OVMF_VARS.fd"),
        target, 12345, 1024, iso_path=Path("installer.iso"), boot_from="cdrom"
    )
    if "once=d,menu=off" not in " ".join(str(part) for part in cdrom_cmd):
        print("[FAIL] installer ISO boot is not pinned with BootNext")
        return 1
    nvme_cmd = make_qemu_cmd(
        "qemu-system-x86_64", "OVMF_CODE.fd", Path("OVMF_VARS.fd"),
        target, 12345, 1024, storage_bus="nvme", extra_disks=(guard,)
    )
    nvme_text = " ".join(str(part) for part in nvme_cmd)
    if "serial=CAPYOSNVME01" not in nvme_text or "serial=CAPYOSNVME02" not in nvme_text:
        print("[FAIL] NVMe multi-disk identities are not deterministic")
        return 1
    with TemporaryDirectory() as temp:
        repo = Path(temp)
        safe_disk = repo / "build/ci/target.img"
        require_safe_disk_path(repo, safe_disk)
        try:
            require_safe_disk_path(repo, repo / "outside.img")
        except ValueError:
            pass
        else:
            print("[FAIL] destructive disk outside build/ci was accepted")
            return 1
        fixture_log = Path(temp) / "installer.log"
        synthetic_recovery_fixture = "-".join(
            ("TEST", "ONLY", "NOTA", "REAL", "KEY0", "0000")
        )
        fixture_log.write_text(
            f"recovery={synthetic_recovery_fixture}\n", encoding="utf-8"
        )
        if extract_volume_key(fixture_log) != synthetic_recovery_fixture:
            print("[FAIL] installer recovery key was not extracted")
            return 1
        if synthetic_recovery_fixture in fixture_log.read_text(encoding="utf-8"):
            print("[FAIL] installer recovery key remained in persisted log")
            return 1
        ovmf_template = Path(temp) / "OVMF_VARS.fd"
        ovmf_template.write_bytes(b"ovmf-template")
        ovmf_log = Path(temp) / "smoke.log"
        stale_ovmf_runtime = ovmf_log.with_name("smoke.OVMF_VARS.runtime.fd")
        stale_ovmf_runtime.write_bytes(b"do-not-overwrite")
        ovmf_runtime = create_runtime_ovmf_vars(ovmf_log, str(ovmf_template))
        if ovmf_runtime == stale_ovmf_runtime:
            print("[FAIL] OVMF runtime reused a stale fixed path")
            return 1
        if stale_ovmf_runtime.read_bytes() != b"do-not-overwrite":
            print("[FAIL] pre-existing OVMF runtime content changed")
            return 1
        if ovmf_runtime.read_bytes() != b"ovmf-template":
            print("[FAIL] run-private OVMF runtime content differs from template")
            return 1
        second_ovmf_runtime = create_runtime_ovmf_vars(
            ovmf_log, str(ovmf_template)
        )
        if second_ovmf_runtime == ovmf_runtime:
            print("[FAIL] OVMF runtimes are not isolated between runs")
            return 1
        cleanup_file(ovmf_runtime)
        cleanup_file(second_ovmf_runtime)
        preexisting = Path(temp) / "preexisting.img"
        preexisting.write_bytes(b"do-not-truncate")
        try:
            prepare_exclusive_disk(preexisting, "16K")
        except FileExistsError:
            pass
        else:
            print("[FAIL] pre-existing destructive disk was truncated")
            return 1
        if preexisting.read_bytes() != b"do-not-truncate":
            print("[FAIL] pre-existing destructive disk content changed")
            return 1
        first = Path(temp) / "guard-a.img"
        second = Path(temp) / "guard-b.img"
        prepare_guard_disk(first, "16K")
        prepare_guard_disk(second, "16K")
        before = file_sha256(first)
        if before != file_sha256(second):
            print("[FAIL] guard disk preparation is not deterministic")
            return 1
        with first.open("r+b") as stream:
            stream.seek(8192)
            stream.write(b"changed")
        if before == file_sha256(first):
            print("[FAIL] guard disk hash did not detect mutation")
            return 1
        descriptor = Path(temp) / "guard.vmdk"
        extent = Path(temp) / "guard-flat.vmdk"
        extent.write_bytes(b"extent")
        descriptor.write_text(
            'version=1\nRW 16 FLAT "guard-flat.vmdk" 0\n', encoding="utf-8"
        )
        if parse_flat_extent(descriptor) != extent.resolve():
            print("[FAIL] VMware flat extent was not resolved")
            return 1
        sized_extent = Path(temp) / "target-flat.vmdk"
        with sized_extent.open("wb") as stream:
            stream.truncate(2 * 1024 * 1024)
        if exact_extent_size_mib(sized_extent) != 2:
            print("[FAIL] VMware extent size was not resolved in exact MiB")
            return 1
        try:
            exact_extent_size_mib(extent)
        except ValueError:
            pass
        else:
            print("[FAIL] non-MiB VMware extent size was accepted")
            return 1
        outside_extent = Path(temp) / "outside-flat.vmdk"
        outside_extent.write_bytes(b"outside")
        nested = Path(temp) / "nested"
        nested.mkdir()
        escaped_descriptor = nested / "escaped.vmdk"
        escaped_descriptor.write_text(
            'version=1\nRW 16 FLAT "../outside-flat.vmdk" 0\n',
            encoding="utf-8",
        )
        try:
            parse_flat_extent(escaped_descriptor)
        except ValueError:
            pass
        else:
            print("[FAIL] escaping VMware FLAT extent was accepted")
            return 1
        stale_evidence = Path(temp) / "installer-evidence.manifest"
        stale_evidence.write_text("result=pass\n", encoding="utf-8")
        stale_temporary = stale_evidence.with_suffix(".manifest.tmp")
        stale_temporary.write_text("partial\n", encoding="utf-8")
        reset_evidence_output(stale_evidence)
        if stale_evidence.exists() or stale_temporary.exists():
            print("[FAIL] stale VMware installer evidence survived reset")
            return 1
        vmx = render_scratch_vmx(
            display_name="CapyOS Scratch",
            iso_path=Path(r"C:\build\CapyOS.iso"),
            target_descriptor=Path(r"C:\scratch\target.vmdk"),
            guard_descriptor=Path(r"C:\scratch\guard.vmdk"),
            pipe_name="capyos-test-pipe",
            boot_from="cdrom",
        )
        if 'firmware = "efi"' not in vmx or 'ethernet0.virtualDev = "e1000"' not in vmx:
            print("[FAIL] VMware scratch VMX lost EFI/E1000 contract")
            return 1
        if 'uefi.secureBoot.enabled = "FALSE"' not in vmx:
            print("[FAIL] VMware scratch VMX did not pin Secure Boot off")
            return 1
        if 'efi.serialConsole.enabled = "FALSE"' not in vmx:
            print("[FAIL] VMware firmware still competes with loader-owned COM1")
            return 1
        if "e1000e" in vmx or 'bios.bootOrder = "cdrom"' not in vmx:
            print("[FAIL] VMware scratch VMX boot/NIC contract changed")
            return 1
        raw_secret = "ABCD-EFGH-IJKL-MNOP-QRST-UVWX"
        sanitized = sanitize_public_text(f"key={raw_secret}\n")
        if contains_recovery_key(sanitized) or raw_secret in sanitized:
            print("[FAIL] VMware public log retained recovery key")
            return 1
        digest_a = "a" * 64
        digest_b = "b" * 64
        evidence = {
            "format": "capyos-installer-wizard-evidence-manifest-v1",
            "release_tag": "0.8.0-alpha.315+20260715",
            "track": "UEFI/GPT/x86_64",
            "provider": "vmware-workstation",
            "iso_artifact": "CapyOS.iso",
            "iso_sha256": digest_a,
            "eligible_target_count": "2",
            "target_selected_explicitly": "yes",
            "target_identity_revalidated": "yes",
            "erase_token_confirmed": "yes",
            "target_sha256_before": digest_a,
            "target_sha256_after": digest_b,
            "target_changed": "yes",
            "guard_sha256_before": digest_a,
            "guard_sha256_after": digest_a,
            "guard_unchanged": "yes",
            "fresh_install_completed": "yes",
            "first_boot_completed": "yes",
            "login_completed": "yes",
            "persistence_marker_written": "yes",
            "persistence_marker_read_after_reboot": "yes",
            "recovery_key_redacted": "yes",
            "recovery_key_included": "no",
            "installer_log_sha256": digest_a,
            "boot1_log_sha256": digest_a,
            "boot2_log_sha256": digest_a,
        }
        rendered = render_evidence(evidence)
        if parse_evidence(rendered) != evidence:
            print("[FAIL] VMware installer evidence did not round-trip")
            return 1
        invalid = dict(evidence)
        invalid["guard_sha256_after"] = digest_b
        try:
            render_evidence(invalid)
        except ValueError:
            pass
        else:
            print("[FAIL] VMware evidence accepted changed guard")
            return 1
    print("[OK] installer smoke contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
