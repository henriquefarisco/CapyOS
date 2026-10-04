"""Production-only one-time bridge material and guest configuration helpers."""
from pathlib import Path
from smoke_x64_update_ab_contract import compare_update_versions
from smoke_x64_helpers import run_cmd, run_cmd_expect_prompt
from update_manifest_common import parse_manifest, payload_metadata
from verify_migration_bridge import verify_bridge

BRIDGE_ROUTE = "https://github.com/henriquefarisco/CapyOS/releases/latest/download/bridge.ini"
BRIDGE_ARGUMENTS = ("production_bridge_manifest", "production_bridge_payload",
                    "production_bridge_version")


def prepare_bridge_material(args, final):
    values = [getattr(args, name, None) for name in BRIDGE_ARGUMENTS]
    if not any(values):
        return None
    if not args.production or not all(values):
        raise ValueError("bridge requires production mode and all three bridge arguments")
    root = Path(args.production_manifest).resolve().parent
    expected = {
        "production_manifest": root / "latest.ini",
        "production_payload": root / "capyos64.bin",
        "production_bridge_manifest": root / "bridge.ini",
        "production_bridge_payload": root / "capyos-bridge64.bin",
    }
    for name, path in expected.items():
        if Path(getattr(args, name)).resolve() != path:
            raise ValueError("migration requires the exact four public assets in one bundle")
    if compare_update_versions(args.production_bridge_version, args.current_version) <= 0:
        raise ValueError("bridge must be newer than the published predecessor")
    fields, _ = parse_manifest((root / "latest.ini").read_bytes())
    verify_bridge(root, final_version=final["version"],
                  bridge_version=args.production_bridge_version,
                  published_at=fields["published_at"], openssl=args.openssl)
    bridge_fields, _ = parse_manifest((root / "bridge.ini").read_bytes())
    size, digest = payload_metadata(root / "capyos-bridge64.bin")
    return {"version": args.production_bridge_version, "manifest_url": BRIDGE_ROUTE,
            "payload_url": bridge_fields["payload_url"], "payload_size": str(size),
            "payload_sha256": digest}


def configure_bridge_route(console, timeout):
    # Existing released line editor; no guest trust anchor or binary is modified.
    run_cmd_expect_prompt(console, "open /system/update/repository.ini", timeout, "open> ",
                          require_command_echo=True)
    for line in ("channel=stable", "branch=main", "source=github:henriquefarisco/CapyOS",
                 "manifest=/system/update/latest.ini", f"remote_manifest={BRIDGE_ROUTE}"):
        run_cmd_expect_prompt(console, line, timeout, "open> ", require_command_echo=True)
    run_cmd(console, ".wq", timeout, expect="file saved")
    run_cmd(console, "print-file /system/update/repository.ini", timeout,
            expect=f"remote_manifest={BRIDGE_ROUTE}", expect_ignore_line_breaks=True)
