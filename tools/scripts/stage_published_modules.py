#!/usr/bin/env python3
"""Stage immutable producer bytes, not machine-dependent source archives."""
import argparse
import hashlib
from pathlib import Path
import sys
from urllib.parse import urlsplit
from urllib.request import Request, urlopen

from build_modules_index import ManifestError, parse_manifest, validate_catalog_manifest
from modules_index_catalog import (
    CANONICAL_FIELDS, MODULE_SPECS, expected_payload_url, pinned_payload_metadata,
    validate_release_tag,
)


def fetch(url, limit, opener=urlopen):
    request = Request(url, headers={"User-Agent": "CapyOS-release-stager/1"})
    with opener(request, timeout=45) as response:
        if response.status != 200 or urlsplit(response.geturl()).scheme != "https":
            raise ManifestError("producer response must be HTTP 200 over HTTPS")
        data = response.read(limit + 1)
        if len(data) > limit:
            raise ManifestError("producer response exceeds its bound")
        return data


def stage(output, release_tag, specs=MODULE_SPECS, opener=urlopen):
    validate_release_tag(release_tag)
    for spec in specs:
        if pinned_payload_metadata(spec) is None:
            raise ManifestError(f"{spec.module_id}: immutable payload pin is required")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    for spec in specs:
        digest, size = pinned_payload_metadata(spec)
        url = expected_payload_url(spec, release_tag)
        payload = fetch(spec.published_payload_mirror or url, size, opener)
        if len(payload) != size or hashlib.sha256(payload).hexdigest() != digest:
            raise ManifestError(f"{spec.module_id}: published payload differs from catalog pin")
        destination = output / spec.repo / "build/capypkg"
        destination.mkdir(parents=True, exist_ok=True)
        manifest_path = destination / (spec.module_id + ".manifest")
        fields = {
            "name": spec.module_id, "version": spec.version,
            "summary": "Official " + spec.module_id, "payload_url": url,
            "payload_sha256": digest, "payload_size": str(size),
            "install_root": spec.install_root, "provides_abi": spec.provides_abi,
            "abi_version": spec.abi_version, "core_abi_min": str(spec.core_abi_min),
            "core_abi_max": str(spec.core_abi_max), "known_good": str(spec.known_good),
            "depends": ",".join(spec.dependencies),
        }
        lines = [f"{key}={fields[key]}" for key in CANONICAL_FIELDS if key in fields]
        lines.append("---")
        manifest_path.write_text("\n".join(lines) + "\n", encoding="ascii")
        (destination / spec.asset).write_bytes(payload)
        validate_catalog_manifest(manifest_path, parse_manifest(manifest_path), spec, release_tag)
        print(f"[ok] staged {spec.module_id}@{spec.version}: {digest}", flush=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path,
                        help="new isolated directory; existing paths are never overwritten")
    parser.add_argument("--release-tag", required=True)
    args = parser.parse_args()
    try:
        stage(args.output, args.release_tag)
    except (OSError, ValueError, ManifestError) as exc:
        print(f"[err] published module staging failed: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
