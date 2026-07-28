#!/usr/bin/env python3
"""Resolve the lab trust key + host endpoint for the signed A/B update gate.

The kernel must be built BEFORE the gate runs, and the build bakes in both the
lab public key and the manifest URL, so those two values have to be decided
first. This helper emits them as a shell-sourceable env file:

    LAB_PRIVATE_KEY=build/ci/update-ab/lab-ed25519.pem
    LAB_PUBLIC_KEY_HEX=<hex64>
    LAB_HOST=10.0.2.2
    LAB_MANIFEST_URL=http://10.0.2.2:18083/latest.ini
    LAB_PAYLOAD_URL=http://10.0.2.2:18083/capyos64.bin

Host address per provider:
  qemu    the fixed SLIRP gateway 10.0.2.2.
  vmware  the host's own address on the NAT network (VMnet8), discovered from
          `ipconfig.exe`; pass --host to override when the lab uses a different
          vmnet or a bridged setup.

The private key is generated once per --out directory and reused while it lives,
so a rebuild plus a gate rerun agree on the anchor. It is a throwaway key: it can
sign nothing that a shipped kernel would accept.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools" / "scripts"))

from smoke_x64_update_ab_contract import (  # noqa: E402  (sys.path tweak above)
    LAB_MANIFEST_NAME,
    LAB_PAYLOAD_NAME,
    LOCAL_HTTP_PORT,
    QEMU_SLIRP_GATEWAY,
    generate_lab_keypair,
)
from update_manifest_common import (  # noqa: E402
    ManifestError,
    raw_public_from_private,
)

_VMNET_SECTION_RE = re.compile(r"VMware Network Adapter (VMnet\d+)", re.IGNORECASE)
_IPV4_RE = re.compile(r"IPv4[^:]*:\s*(\d+\.\d+\.\d+\.\d+)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider", choices=("qemu", "vmware"), required=True)
    parser.add_argument("--out", default="build/ci/update-ab/lab.env", type=Path)
    parser.add_argument(
        "--private-key", default="build/ci/update-ab/lab-ed25519.pem", type=Path
    )
    parser.add_argument("--host", help="override the host address the guest dials")
    parser.add_argument("--vmnet", default="VMnet8", help="VMware NAT adapter name")
    parser.add_argument("--port", type=int, default=LOCAL_HTTP_PORT)
    parser.add_argument("--openssl", default="openssl")
    return parser.parse_args()


def vmnet_host_address(text: str, vmnet: str) -> str:
    """Pick the host IPv4 bound to the named VMware virtual adapter.

    `ipconfig` groups adapters into blocks headed by the adapter name, so scan
    forward from the requested header until the first IPv4 line.
    """
    lines = text.splitlines()
    for index, line in enumerate(lines):
        match = _VMNET_SECTION_RE.search(line)
        if not match or match.group(1).lower() != vmnet.lower():
            continue
        for candidate in lines[index + 1 : index + 12]:
            found = _IPV4_RE.search(candidate)
            if found:
                return found.group(1)
        break
    raise RuntimeError(f"could not find an IPv4 address for {vmnet}")


def resolve_host(args: argparse.Namespace) -> str:
    if args.host:
        return args.host
    if args.provider == "qemu":
        return QEMU_SLIRP_GATEWAY
    completed = subprocess.run(
        ["ipconfig.exe"], check=True, capture_output=True, text=True, errors="replace"
    )
    return vmnet_host_address(completed.stdout, args.vmnet)


def main() -> int:
    args = parse_args()
    private_key = (REPO_ROOT / args.private_key).resolve()
    out_path = (REPO_ROOT / args.out).resolve()
    try:
        host = resolve_host(args)
        if private_key.exists():
            public_hex = raw_public_from_private(args.openssl, private_key).hex()
        else:
            public_hex = generate_lab_keypair(args.openssl, private_key)
        base = f"http://{host}:{args.port}"
        out_path.parent.mkdir(parents=True, exist_ok=True)
        # Single-quoted: the workspace path contains spaces, and `.`-sourcing an
        # unquoted assignment would split the key path into two shell words.
        out_path.write_text(
            "".join(
                f"{key}='{value}'\n"
                for key, value in (
                    ("LAB_PRIVATE_KEY", private_key),
                    ("LAB_PUBLIC_KEY_HEX", public_hex),
                    ("LAB_HOST", host),
                    ("LAB_MANIFEST_URL", f"{base}/{LAB_MANIFEST_NAME}"),
                    ("LAB_PAYLOAD_URL", f"{base}/{LAB_PAYLOAD_NAME}"),
                )
            ),
            encoding="ascii",
        )
        print(f"[ok] lab trust anchor ready for {args.provider}: {out_path}")
        print(f"[ok] guest will fetch {base}/{LAB_MANIFEST_NAME}")
        return 0
    except (ManifestError, RuntimeError, OSError, subprocess.CalledProcessError) as exc:
        print(f"[err] {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
