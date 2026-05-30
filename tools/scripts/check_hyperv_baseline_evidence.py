#!/usr/bin/env python3
"""Validate manually collected Hyper-V Gen2 baseline evidence."""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

from hyperv_baseline_evidence_core import (
    GATE_PROFILES,
    HOST_PREFLIGHT_PROFILES,
    VALIDATION_PROFILES,
    discover_bundle_logs,
    merge_evidence_paths,
    read_evidence_paths,
    read_evidence_text,
    result_summary,
    validate_evidence,
    write_summary,
)

def run_self_test() -> int:
    passing = """
[vmbus] INITIATE_CONTACT v0x50000
runtime-native show
platform=vmbus vmbus=offers storage-fw=off
net-status
driver=hyperv-netvsc runtime=degraded vmbus=offers stage=offers
net-dump-runtime
vmbus=offers stage=channel relid=0x2 conn=0x14
recovery-storage
storage fallback=ramdisk storvsc=stage:offers
info
[*] GPT: entries @ LBA 2, count=128, size=128
    GPT[00] BOOT: LBA=2048..4095
[*] Manifest @ LBA 2048: magic=0x43415059 ver=1 entries=2
[*] Kernel (gpt:manifest) rel_lba=16 abs_lba=2064 sec=128
"""
    result = validate_evidence(
        passing,
        require_inspect_disk=True,
        min_vmbus_stage="offers",
        min_runtime_stage="channel",
    )
    if (
        not result.ok()
        or "offers" not in result.vmbus_values
        or "channel" not in result.stage_values
        or result.next_slice != "StorVSC storage"
        or not result.inspect_disk_present
    ):
        print("[err] passing evidence self-test failed", file=sys.stderr)
        return 1

    failing = """
runtime-native show
panic: synthetic failure
net-status
vmbus=off
"""
    result = validate_evidence(failing)
    if result.ok() or not any("panic:" in err for err in result.errors):
        print("[err] failing evidence self-test failed", file=sys.stderr)
        return 1
    if result.next_slice != "boot/VMBus contact":
        print("[err] failing recommendation self-test failed", file=sys.stderr)
        return 1
    if result.failure_focus != "kernel panic/fault":
        print("[err] failing focus self-test failed", file=sys.stderr)
        return 1

    result = validate_evidence(net_followup := """
runtime-native show
platform=vmbus vmbus=ready storage-fw=off
net-status
driver=hyperv-netvsc runtime=degraded vmbus=ready stage=ready
net-dump-runtime
vmbus=ready stage=ready relid=0x2 conn=0x14
recovery-storage
storage persistent volume=mounted storvsc=stage:ready
""", require_inspect_disk=True)
    if result.ok() or result.failure_focus != "disk inspection evidence":
        print("[err] inspect-disk requirement self-test failed", file=sys.stderr)
        return 1

    result = validate_evidence(
        net_followup.replace("stage=ready", "stage=channel"),
        min_runtime_stage="control",
    )
    if result.ok() or result.failure_focus != "minimum stage requirement":
        print("[err] minimum-stage self-test failed", file=sys.stderr)
        return 1

    strict_missing = """
[vmbus] INITIATE_CONTACT v0x50000
runtime-native show
platform=vmbus vmbus=offers storage-fw=off
net-status
driver=hyperv-netvsc runtime=degraded vmbus=offers stage=offers
recovery-storage
storage fallback=ramdisk storvsc=stage:offers
"""
    result = validate_evidence(strict_missing, strict_commands=True)
    if result.ok() or result.failure_focus != "required command evidence":
        print("[err] strict-command self-test failed", file=sys.stderr)
        return 1

    result = validate_evidence(net_followup)
    if not result.ok() or result.next_slice != "NetVSC network":
        print("[err] NetVSC recommendation self-test failed", file=sys.stderr)
        return 1

    result = validate_evidence(
        net_followup,
        expect_next_slice="netvsc-network",
        expect_failure_focus="baseline-accepted",
    )
    if not result.ok() or result.expected_next_slice != "NetVSC network":
        print("[err] expectation accepted self-test failed", file=sys.stderr)
        return 1

    result = validate_evidence(net_followup, expect_next_slice="storvsc-storage")
    if result.ok() or result.failure_focus != "expectation mismatch":
        print("[err] expectation mismatch self-test failed", file=sys.stderr)
        return 1

    result = validate_evidence(net_followup, gate_profile="net-ready")
    if result.ok() or result.failure_focus != "gate profile requirement":
        print("[err] net-ready profile self-test failed", file=sys.stderr)
        return 1

    net_ready = net_followup.replace("runtime=degraded", "runtime=ready")
    result = validate_evidence(net_ready, gate_profile="net-ready")
    if not result.ok() or result.failure_focus != "baseline accepted":
        print("[err] net-ready accepted self-test failed", file=sys.stderr)
        return 1

    result = validate_evidence(passing, gate_profile="storage-persistent")
    if result.ok() or result.failure_focus != "gate profile requirement":
        print("[err] storage-persistent profile self-test failed", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        serial_log = tmp / "serial.log"
        commands_log = tmp / "commands.log"
        inspect_log = tmp / "inspect-disk.log"
        serial_log.write_text(
            "[vmbus] INITIATE_CONTACT v0x50000\n"
            "runtime-native show\n"
            "platform=vmbus vmbus=offers storage-fw=off\n",
            encoding="utf-8",
        )
        commands_log.write_text(
            "net-status\n"
            "driver=hyperv-netvsc runtime=degraded vmbus=offers stage=offers\n"
            "net-dump-runtime\n"
            "vmbus=offers stage=channel relid=0x2 conn=0x14\n"
            "recovery-storage\n"
            "storage fallback=ramdisk storvsc=stage:offers\n",
            encoding="utf-8",
        )
        inspect_log.write_text(
            "[*] GPT: entries @ LBA 2, count=128, size=128\n"
            "    GPT[00] BOOT: LBA=2048..4095\n"
            "[*] Manifest @ LBA 2048: magic=0x43415059 ver=1 entries=2\n"
            "[*] Kernel (gpt:manifest) rel_lba=16 abs_lba=2064 sec=128\n",
            encoding="utf-8",
        )
        preflight_log = tmp / "hyperv-preflight.txt"
        preflight_log.write_text(
            "Hyper-V preflight\n"
            "elevated=True\n"
            "hyperv_module=Hyper-V\n"
            "selected_vm=CapyOS-HyperV\n"
            "generation=2\n"
            "secure_boot=False\n"
            "dynamic_memory_enabled=False\n",
            encoding="utf-8",
        )
        text = read_evidence_text(serial_log, [commands_log, inspect_log])
        result = validate_evidence(text, require_inspect_disk=True, strict_commands=True)
        result.source_logs = [str(serial_log), str(commands_log), str(inspect_log)]
        if not result.ok() or not result.inspect_disk_present or result.source_logs[2] != str(inspect_log):
            print("[err] multi-log evidence self-test failed", file=sys.stderr)
            return 1

        bundle_paths = discover_bundle_logs(tmp)
        if bundle_paths != [serial_log, commands_log, inspect_log, preflight_log]:
            print("[err] bundle discovery self-test failed", file=sys.stderr)
            return 1
        merged_paths = merge_evidence_paths(None, [], bundle_paths)
        text = read_evidence_paths(merged_paths)
        result = validate_evidence(
            text,
            require_inspect_disk=True,
            strict_commands=True,
            require_host_preflight=True,
        )
        result.source_logs = [str(path) for path in merged_paths]
        if (
            not result.ok()
            or not result.host_preflight_present
            or result.source_logs != [
                str(serial_log),
                str(commands_log),
                str(inspect_log),
                str(preflight_log),
            ]
        ):
            print("[err] bundle validation self-test failed", file=sys.stderr)
            return 1
        bundle_result = result

        missing_preflight = read_evidence_text(serial_log, [commands_log, inspect_log])
        result = validate_evidence(missing_preflight, require_host_preflight=True)
        if result.ok() or result.failure_focus != "host preflight evidence":
            print("[err] host-preflight requirement self-test failed", file=sys.stderr)
            return 1

        summary_path = Path(tmpdir) / "summary.json"
        write_summary(summary_path, result_summary(bundle_result, Path("sample.log")))
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        if (
            summary.get("ok") is not True
            or summary.get("strict_commands") is not True
            or summary.get("host_preflight_present") is not True
            or summary.get("source_logs")
            != [str(serial_log), str(commands_log), str(inspect_log), str(preflight_log)]
        ):
            print("[err] summary self-test failed", file=sys.stderr)
            return 1

    print("[ok] Hyper-V baseline evidence self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", help="Manual Hyper-V baseline evidence log")
    parser.add_argument(
        "--extra-log",
        action="append",
        default=[],
        help="Additional evidence log; may be passed more than once",
    )
    parser.add_argument("--bundle-dir", help="Directory containing standard Hyper-V evidence files")
    parser.add_argument("--summary", help="Write JSON validation summary")
    parser.add_argument(
        "--require-inspect-disk",
        action="store_true",
        help="Require captured make inspect-disk evidence in the log",
    )
    parser.add_argument("--min-vmbus-stage", default="", help="Minimum accepted vmbus= stage")
    parser.add_argument("--min-runtime-stage", default="", help="Minimum accepted stage= phase")
    parser.add_argument(
        "--profile",
        default="baseline",
        choices=GATE_PROFILES,
        help="Gate profile with additional host-only evidence requirements",
    )
    parser.add_argument(
        "--strict-commands",
        action="store_true",
        help="Treat missing mandatory command outputs as errors",
    )
    parser.add_argument(
        "--require-host-preflight",
        action="store_true",
        help="Require captured Hyper-V host preflight evidence",
    )
    parser.add_argument(
        "--require-host-serial-console",
        action="store_true",
        help="Require Hyper-V preflight COM/serial console evidence",
    )
    parser.add_argument(
        "--require-host-network-adapter",
        action="store_true",
        help="Require Hyper-V preflight synthetic network adapter evidence",
    )
    parser.add_argument(
        "--require-host-storage-disk",
        action="store_true",
        help="Require Hyper-V preflight SCSI disk evidence",
    )
    parser.add_argument(
        "--host-preflight-profile",
        default="none",
        choices=HOST_PREFLIGHT_PROFILES,
        help="Aggregate Hyper-V host preflight requirement profile",
    )
    parser.add_argument(
        "--validation-profile",
        default="none",
        choices=VALIDATION_PROFILES,
        help="Aggregate Hyper-V evidence validation profile",
    )
    parser.add_argument(
        "--require-bundle-file",
        action="append",
        default=[],
        help="Require a bundle file class: serial, commands, inspect-disk or preflight",
    )
    parser.add_argument("--expect-next-slice", default="", help="Expected next= recommendation")
    parser.add_argument("--expect-focus", default="", help="Expected focus= classification")
    parser.add_argument("--self-test", action="store_true", help="Run parser self-test")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()
    if not args.log and not args.bundle_dir:
        print("[err] --log or --bundle-dir is required unless --self-test is used", file=sys.stderr)
        return 2

    log_path = Path(args.log) if args.log else None
    extra_log_paths = [Path(extra) for extra in args.extra_log]
    try:
        bundle_paths = discover_bundle_logs(Path(args.bundle_dir)) if args.bundle_dir else []
    except ValueError as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2
    evidence_paths = merge_evidence_paths(log_path, extra_log_paths, bundle_paths)
    if not evidence_paths:
        print("[err] no evidence files found", file=sys.stderr)
        return 2
    text = read_evidence_paths(evidence_paths)
    result = validate_evidence(
        text,
        require_inspect_disk=args.require_inspect_disk,
        min_vmbus_stage=args.min_vmbus_stage,
        min_runtime_stage=args.min_runtime_stage,
        gate_profile=args.profile,
        strict_commands=args.strict_commands,
        require_host_preflight=args.require_host_preflight,
        require_host_serial_console=args.require_host_serial_console,
        require_host_network_adapter=args.require_host_network_adapter,
        require_host_storage_disk=args.require_host_storage_disk,
        validation_profile=args.validation_profile,
        host_preflight_profile=args.host_preflight_profile,
        source_paths=evidence_paths,
        required_bundle_files=args.require_bundle_file,
        expect_next_slice=args.expect_next_slice,
        expect_failure_focus=args.expect_focus,
    )
    result.source_logs = [str(path) for path in evidence_paths]
    summary: dict[str, object] | None = None
    if args.summary:
        summary = result_summary(result, evidence_paths[0])
        write_summary(Path(args.summary), summary)
    for warning in result.warnings:
        print(f"[warn] {warning}")
    if not result.ok():
        for error in result.errors:
            print(f"[err] {error}", file=sys.stderr)
        print(
            f"[hint] focus={result.failure_focus} next={result.next_slice}",
            file=sys.stderr,
        )
        if summary is None:
            summary = result_summary(result, evidence_paths[0])
        primary_action = summary["ci_acceptance_primary_action"]
        action = str(primary_action.get("action", ""))
        if action:
            print(f"[action] {action}", file=sys.stderr)
            print(f"[action-reason] {primary_action.get('reason', '')}", file=sys.stderr)
            print(f"[action-category] {primary_action.get('category', '')}", file=sys.stderr)
        return 1

    print(
        "[ok] Hyper-V baseline evidence accepted: "
        f"vmbus={','.join(result.vmbus_values)} "
        f"stage={','.join(result.stage_values)} "
        f"next={result.next_slice} "
        f"focus={result.failure_focus}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
