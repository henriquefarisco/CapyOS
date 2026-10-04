"""Resolve the one-time bridge from the exact release's source-owned policy."""
import argparse
import json
from pathlib import Path
import re
import subprocess
from update_manifest_common import ManifestError
from verify_migration_bridge import stable_key

ROOT = Path(__file__).resolve().parents[2]
POLICY = ".github/release-policy/migration-bridge.json"
HEADER = "include/core/runtime_version.h"


def unique_fields(pairs):
    fields = {}
    for key, value in pairs:
        if key in fields:
            raise ValueError("duplicate bridge policy field")
        fields[key] = value
    return fields


def resolve_policy(raw, header, version):
    if raw is None:
        return ""  # Historical releases have no migration contract.
    try:
        fields = json.loads(raw, object_pairs_hook=unique_fields)
        if not isinstance(fields, dict) or set(fields) != {"final_version", "bridge_version"}:
            raise ValueError("unexpected bridge policy fields")
        final, bridge = fields["final_version"], fields["bridge_version"]
        if not isinstance(final, str) or not isinstance(bridge, str) or stable_key(bridge) >= stable_key(final):
            raise ValueError("invalid bridge version ordering")
    except (ValueError, TypeError) as exc:
        raise ManifestError("invalid migration bridge policy") from exc
    if version != final:
        return ""  # Future stable/alpha releases do not inherit this one-time bridge.
    identities = re.findall(r'^#define CAPYOS_RUNTIME_VERSION_FULL\s+"([^"]+)"', header, re.M)
    if identities != [bridge]:
        raise ManifestError("tagged bridge policy/runtime identity mismatch")
    return bridge


def read_tag_file(commit, path):
    tree = subprocess.run(["git", "ls-tree", commit, "--", path], cwd=ROOT,
                          capture_output=True, text=True, check=True).stdout
    if not tree:
        return None
    if not tree.startswith("100644 blob "):
        raise ManifestError("tagged bridge policy/header must be regular files")
    return subprocess.run(["git", "show", f"{commit}:{path}"], cwd=ROOT,
                          capture_output=True, text=True, check=True).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-version")
    parser.add_argument("--tag-commit")
    parser.add_argument("--require-bridge", action="store_true")
    args = parser.parse_args()
    if args.tag_commit and not re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", args.tag_commit):
        parser.error("tag commit must be a resolved full commit SHA")
    if args.tag_commit and not args.release_version:
        parser.error("tagged policy requires the resolved release version")
    try:
        if args.tag_commit:
            raw = read_tag_file(args.tag_commit, POLICY)
            header = read_tag_file(args.tag_commit, HEADER) or ""
        else:
            path = ROOT / POLICY
            if path.is_symlink():
                raise ManifestError("bridge policy cannot be a symlink")
            raw = path.read_text() if path.exists() else None
            header = (ROOT / HEADER).read_text() if raw is not None else ""
        version = args.release_version
        if version is None:
            identity = re.search(r'^#define CAPYOS_VERSION_FULL\s+"([^"]+)"',
                                 (ROOT / "include/core/version.h").read_text(), re.M)
            if not identity:
                raise ManifestError("missing canonical release identity")
            version = identity[1]
        bridge = resolve_policy(raw, header, version)
        if args.require_bridge and not bridge:
            raise ManifestError("this release has no active migration bridge contract")
    except (OSError, ValueError, ManifestError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"[err] {exc}\n")
    print(bridge)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
