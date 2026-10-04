#!/usr/bin/env python3
"""Validate the payload against the full or one-time legacy migration limit."""
import argparse
from pathlib import Path

from update_manifest_common import ManifestError, PAYLOAD_MAX_BYTES, payload_metadata

LEGACY_CACHE_MAX_BYTES = (12 + 4096 // 4) * 4096


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--legacy-cache", action="store_true",
                        help="Enforce the CapyFS v2 single-file bound for a migration bridge")
    args = parser.parse_args()
    try:
        size, digest = payload_metadata(args.payload)
    except ManifestError as exc:
        print(f"[err] update payload budget: {exc}")
        return 1
    limit = LEGACY_CACHE_MAX_BYTES if args.legacy_cache else PAYLOAD_MAX_BYTES
    if size > limit:
        print(f"[err] update payload: {size}/{limit} bytes exceeds the cache limit")
        return 1
    print(f"[ok] update payload: {size}/{limit} bytes, sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
