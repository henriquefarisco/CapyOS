#!/usr/bin/env python3
"""Capture bounded, current HTML fixtures for the CapyBrowser QEMU smoke.

The downloaded third-party HTML is deliberately written below ``build/ci``
(ignored by Git).  A manifest records provenance, capture time, byte counts and
hashes so a smoke run can prove exactly which bounded bytes it served.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_DIR = REPO_ROOT / "build" / "ci" / "capygfx_real_sites"
MAX_BROWSER_BODY_BYTES = 512 * 1024
MAX_CAPTURE_BYTES = 8 * 1024 * 1024
USER_AGENT = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/134.0.0.0 Safari/537.36"
)
SITES = (
    ("youtube", "https://www.youtube.com/"),
    ("wikipedia", "https://www.wikipedia.org/"),
    ("tumblr", "https://www.tumblr.com/"),
)


@dataclass(frozen=True)
class Capture:
    name: str
    source_url: str
    final_url: str
    status: int
    content_type: str
    original_bytes: int
    served: bytes
    original_sha256: str


def _capture_once(name: str, url: str, timeout: float,
                  max_served_bytes: int) -> Capture:
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": USER_AGENT,
            "Accept": "text/html,application/xhtml+xml;q=0.9,*/*;q=0.1",
            "Accept-Language": "en-US,en;q=0.9",
            # urllib does not transparently decode compressed responses.  Ask
            # for identity bytes so the fixture is directly consumable by the
            # guest's deliberately small HTTP client.
            "Accept-Encoding": "identity",
            "Connection": "close",
        },
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        status = int(getattr(response, "status", 200))
        content_type = response.headers.get("Content-Type", "")
        content_encoding = response.headers.get("Content-Encoding", "identity")
        if content_encoding.strip().lower() not in ("", "identity"):
            raise RuntimeError(
                f"{name}: unsupported Content-Encoding {content_encoding!r}"
            )
        if "html" not in content_type.lower():
            raise RuntimeError(
                f"{name}: expected HTML Content-Type, got {content_type!r}"
            )

        kept = bytearray()
        digest = hashlib.sha256()
        total = 0
        while True:
            chunk = response.read(64 * 1024)
            if not chunk:
                break
            total += len(chunk)
            if total > MAX_CAPTURE_BYTES:
                raise RuntimeError(
                    f"{name}: response exceeds capture safety limit "
                    f"({MAX_CAPTURE_BYTES} bytes)"
                )
            digest.update(chunk)
            room = max_served_bytes - len(kept)
            if room > 0:
                kept.extend(chunk[:room])

        if total == 0:
            raise RuntimeError(f"{name}: empty response")
        return Capture(
            name=name,
            source_url=url,
            final_url=response.geturl(),
            status=status,
            content_type=content_type,
            original_bytes=total,
            served=bytes(kept),
            original_sha256=digest.hexdigest(),
        )


def capture_with_retries(name: str, url: str, retries: int, timeout: float,
                         max_served_bytes: int) -> Capture:
    last_error: BaseException | None = None
    for attempt in range(1, retries + 1):
        try:
            return _capture_once(name, url, timeout, max_served_bytes)
        except (OSError, RuntimeError, urllib.error.URLError) as exc:
            last_error = exc
            if attempt >= retries:
                break
            delay = min(8.0, float(2 ** (attempt - 1)))
            print(
                f"[warn] {name}: capture attempt {attempt}/{retries} failed: "
                f"{exc}; retrying in {delay:.0f}s",
                file=sys.stderr,
            )
            time.sleep(delay)
    raise RuntimeError(
        f"{name}: capture failed after {retries} attempts: {last_error}"
    )


def _atomic_write(path: Path, payload: bytes) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(payload)
    temporary.replace(path)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        default=str(DEFAULT_OUTPUT_DIR),
        help="Destination below build/ci (default: %(default)s)",
    )
    parser.add_argument("--retries", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--max-served-bytes",
        type=int,
        default=MAX_BROWSER_BODY_BYTES,
        help="Per-site bytes retained for the guest (maximum 524288)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.retries < 1:
        print("[err] --retries must be at least 1", file=sys.stderr)
        return 2
    if args.timeout <= 0:
        print("[err] --timeout must be positive", file=sys.stderr)
        return 2
    if not 1 <= args.max_served_bytes <= MAX_BROWSER_BODY_BYTES:
        print(
            f"[err] --max-served-bytes must be in 1..{MAX_BROWSER_BODY_BYTES}",
            file=sys.stderr,
        )
        return 2

    output_dir = Path(args.output_dir).expanduser().resolve()
    try:
        captures = [
            capture_with_retries(
                name, url, args.retries, args.timeout, args.max_served_bytes
            )
            for name, url in SITES
        ]
        output_dir.mkdir(parents=True, exist_ok=True)

        records = []
        for capture in captures:
            filename = f"{capture.name}.html"
            served_sha256 = hashlib.sha256(capture.served).hexdigest()
            _atomic_write(output_dir / filename, capture.served)
            records.append(
                {
                    "name": capture.name,
                    "source_url": capture.source_url,
                    "final_url": capture.final_url,
                    "http_status": capture.status,
                    "content_type": capture.content_type,
                    "original_bytes": capture.original_bytes,
                    "served_bytes": len(capture.served),
                    "truncated": capture.original_bytes > len(capture.served),
                    "original_sha256": capture.original_sha256,
                    "served_sha256": served_sha256,
                    "file": filename,
                }
            )
            print(
                f"[ok] {capture.name}: {capture.original_bytes} bytes captured, "
                f"{len(capture.served)} served, sha256={served_sha256}"
            )

        manifest = {
            "schema_version": 1,
            "captured_at_utc": datetime.now(timezone.utc).isoformat(),
            "user_agent": USER_AGENT,
            "max_served_bytes": args.max_served_bytes,
            "sites": records,
        }
        _atomic_write(
            output_dir / "manifest.json",
            (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"),
        )
        print(f"[ok] manifest: {output_dir / 'manifest.json'}")
        return 0
    except (OSError, RuntimeError) as exc:
        print(f"[err] real-site capture failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
