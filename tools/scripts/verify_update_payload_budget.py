#!/usr/bin/env python3
"""Fail the build before publication if legacy updaters cannot fetch its kernel."""
import argparse
from pathlib import Path

from update_manifest_common import ManifestError, PAYLOAD_MAX_BYTES, payload_metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", type=Path, required=True)
    args = parser.parse_args()
    try:
        size, digest = payload_metadata(args.payload)
    except ManifestError as exc:
        print(f"[err] update payload budget: {exc}")
        return 1
    print(f"[ok] update payload: {size}/{PAYLOAD_MAX_BYTES} bytes, sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
