#!/usr/bin/env python3

"""Host-only guard for Hyper-V input/VMBus log hygiene.

Interactive keyboard packets and SIMP notifications are hot-path events. They
must stay quiet by default so pressing a key in Hyper-V does not flood the
framebuffer. The detailed packet trace remains available when building with
CAPYOS_HYPERV_VERBOSE_IO.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
RING = REPO_ROOT / "src/drivers/hyperv/vmbus_ring.c"
TRANSPORT = REPO_ROOT / "src/drivers/hyperv/vmbus_transport.c"
KEYBOARD = REPO_ROOT / "src/drivers/hyperv/vmbus_keyboard_protocol.c"


def fail(message: str) -> int:
    print(f"[err] {message}", file=sys.stderr)
    return 1


def require(pattern: str, text: str, message: str) -> int:
    if not re.search(pattern, text, re.DOTALL):
        return fail(message)
    return 0


def main() -> int:
    ring = RING.read_text(encoding="utf-8")
    transport = TRANSPORT.read_text(encoding="utf-8")
    keyboard = KEYBOARD.read_text(encoding="utf-8")

    checks = [
        require(
            r"static int ring_verbose_io\(void\).*CAPYOS_HYPERV_VERBOSE_IO",
            ring,
            "vmbus_ring.c must keep packet/signal traces behind CAPYOS_HYPERV_VERBOSE_IO",
        ),
        require(
            r"static void ring_log\(.*?if \(!ring_verbose_io\(\)\)",
            ring,
            "ring_log must be quiet unless verbose I/O tracing is enabled",
        ),
        require(
            r"static int vmbus_verbose_io\(void\).*CAPYOS_HYPERV_VERBOSE_IO",
            transport,
            "vmbus_transport.c must define the verbose I/O guard",
        ),
        require(
            # The SIMP SINT trace must stay behind the verbose-IO guard. The
            # inner call may be fbcon_print or the centralized
            # vmbus_transport_log wrapper (which itself re-checks the guard and
            # is a no-op under UNIT_TEST); either way the outer
            # `if (vmbus_verbose_io())` keeps it quiet on every keypress.
            r'if \(vmbus_verbose_io\(\)\)\s*\{\s*'
            r'(?:fbcon_print|vmbus_transport_log)\("\[vmbus\] SIMP SINT="\)',
            transport,
            "SIMP VMBus notifications must not log on every keypress by default",
        ),
        require(
            r"static int protocol_verbose_io\(void\).*CAPYOS_HYPERV_VERBOSE_IO",
            keyboard,
            "keyboard protocol must define the verbose I/O guard",
        ),
        require(
            r"protocol_log_packet_desc\(.*?if \(!protocol_verbose_io\(\)\)",
            keyboard,
            "keyboard packet descriptors must be quiet by default",
        ),
        require(
            r"msgtype != SYNTH_KBD_EVENT \|\| protocol_verbose_io\(\)",
            keyboard,
            "keyboard key events must not print msgtype for each keystroke",
        ),
    ]
    for rc in checks:
        if rc != 0:
            return rc

    print("[ok] Hyper-V quiet input policy self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
