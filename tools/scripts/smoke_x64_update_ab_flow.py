#!/usr/bin/env python3
"""Boot phases of the Etapa 8 signed A/B update gate.

Every function here drives a console duck-type that satisfies the SmokeSession
surface (`marker`, `send_line`, `wait_for`, `wait_for_any`, `tail`, `text`), so
the QEMU driver and the VMware driver share one behavioural definition and
cannot drift apart. The host HTTP endpoint lives here too because both drivers
need the guest to reach the same signed material.

Legal boot sequence proven by these phases (see the boot-slot lifecycle: the
loader owns attempt consumption and rollback, the updater only observes it):

  boot 1  provider ready -> fetch -> download -> prepare -> apply -> reboot
  boot 2  attempt pending -> confirm health -> re-prepare -> apply -> reboot
  boot 3  attempt pending -> deliberately NOT confirmed -> reboot
  boot 4  loader-applied rollback observed and reported

Ordering constraints that are load-bearing, not stylistic:
  * confirm-health must be the FIRST boot-slot mutation of its boot, because it
    matches against the generation the loader committed into the attempt token;
    any earlier durable write invalidates the token (-60 token-mismatch).
  * confirm-health clears the staged catalog, payload cache and state file, so a
    second apply in the same boot must re-run prepare first (otherwise -50/-30).
"""

from __future__ import annotations

import http.server
import threading
from pathlib import Path

from smoke_x64_helpers import run_cmd
from smoke_x64_update_ab_contract import (
    APPLY_OK,
    APPLY_SUMMARY,
    ARMED_ATTEMPT_EXPECTATIONS,
    ATTEMPT_PENDING_SUMMARY,
    CONFIRM_OK,
    CONFIRM_SUMMARY,
    DOWNLOAD_OK,
    FETCH_OK,
    LOCAL_HTTP_PORT,
    MANIFEST_NOT_NEWER_SUMMARY,
    PREPARE_EXPLAIN_CLEAN,
    PREPARE_OK,
    PROVIDER_READY_LINE,
    ROLLBACK_OK,
    ROLLBACK_SUMMARY,
    attempt_marker,
)


def start_local_http_server(
    root: Path, port: int = LOCAL_HTTP_PORT
) -> http.server.ThreadingHTTPServer:
    """Serve the signed manifest + payload to the guest over SLIRP/NAT.

    Bound on 0.0.0.0 because the guest reaches the host through the gateway
    address (10.0.2.2 under QEMU user-net, the NAT host under VMware). Threading
    server: the guest opens one connection per request and the payload transfer
    must not block a concurrent manifest fetch.
    """
    directory = str(root)

    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=directory, **kwargs)

        def log_message(self, *args):  # silence per-request stderr noise
            pass

    server = http.server.ThreadingHTTPServer(("0.0.0.0", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def require_boot_attempt(text: str, slot: int, state: str) -> None:
    """Fail unless this boot came from the expected slot in the expected state."""
    marker = attempt_marker(slot, state)
    if marker not in text:
        raise RuntimeError(f"boot log does not contain {marker!r}")
    for other_slot in (0, 1):
        for other_state in ("confirmed", "pending", "rollback"):
            if (other_slot, other_state) == (slot, state):
                continue
            unexpected = attempt_marker(other_slot, other_state)
            if unexpected in text:
                raise RuntimeError(f"boot log also reports {unexpected!r}")


def assert_provider_ready(session, timeout: float) -> None:
    run_cmd(session, "print-boot-slot", timeout, expect=PROVIDER_READY_LINE)


def assert_http_endpoint_reachable(
    session, timeout: float, url: str, *, attempts: int = 1
) -> None:
    """Require HTTP 200, tolerating only an explicitly bounded transient retry."""
    if attempts < 1:
        raise ValueError("endpoint reachability attempts must be positive")
    for _attempt in range(attempts):
        marker = session.marker()
        session.send_line(f"net-fetch {url}")
        outcome = session.wait_for_any(
            ("status=200", "diag:"),
            timeout=min(timeout, 60.0),
            start_at=marker,
        )
        session.wait_for("> ", timeout=min(timeout, 60.0), start_at=marker)
        if outcome == "status=200":
            return
    for command in (
        "net-status",
        "net-ip",
        "net-gw",
        "hey gateway",
    ):
        run_cmd(session, command, min(timeout, 30.0))
    raise RuntimeError(
        "guest could not reach the signed-update lab endpoint after "
        f"{attempts} attempt(s):\n" + session.tail(5200)
    )


def assert_armed_attempt_state(session, timeout: float) -> None:
    """Prove the durable post-apply lifecycle in one slot-manager snapshot.

    Arming is not merely a staged/valid payload state. The boot-slot lifecycle
    immediately makes the candidate active, leaves its health unconfirmed and
    keeps rollback armed until a later boot is explicitly confirmed healthy.
    All three facts must come from the same ``print-boot-slot`` invocation so a
    stale line from an earlier command cannot satisfy the gate.
    """
    marker = session.marker()
    session.send_line("print-boot-slot")
    for expected in ARMED_ATTEMPT_EXPECTATIONS:
        session.wait_for(
            expected,
            timeout=timeout,
            start_at=marker,
            ignore_line_breaks=True,
        )
    session.wait_for("> ", timeout=timeout, start_at=marker)


def stage_and_arm_update(
    session,
    timeout: float,
    *,
    expect_version: str,
    expect_payload_url: str | None = None,
    expect_payload_sha256: str | None = None,
) -> None:
    """fetch -> download -> preflight -> prepare -> apply on the inactive slot."""
    run_cmd(session, "update-fetch", timeout * 4, expect=FETCH_OK)
    run_cmd(session, "update-status", timeout, expect=f"available={expect_version}")
    if expect_payload_url is not None:
        run_cmd(
            session,
            "update-status",
            timeout,
            expect=f"payload={expect_payload_url}",
            expect_ignore_line_breaks=True,
        )
    run_cmd(session, "update-download-payload", timeout * 8, expect=DOWNLOAD_OK)
    if expect_payload_sha256 is not None:
        run_cmd(
            session,
            "update-status",
            timeout,
            expect=f"sha256={expect_payload_sha256}",
            expect_ignore_line_breaks=True,
        )
    run_cmd(
        session,
        "update-prepare-explain",
        timeout,
        expect=PREPARE_EXPLAIN_CLEAN,
    )
    run_cmd(session, "update-prepare", timeout * 8, expect=PREPARE_OK)
    run_cmd(session, "update-apply", timeout * 4, expect=APPLY_OK)
    run_cmd(session, "update-status", timeout, expect=APPLY_SUMMARY)
    run_cmd(session, "update-status", timeout, expect="rc=0")


def restage_and_arm_update(session, timeout: float, *, expect_version: str) -> None:
    """Second cycle after confirm-health wiped staged.ini/payload.bin/state.ini.

    The catalog (`latest.ini`) survives the clear, so this path re-downloads the
    payload instead of re-fetching the manifest: it exercises the same verified
    write while keeping the extra network round trip out of the gate.
    """
    run_cmd(session, "update-status", timeout, expect=f"available={expect_version}")
    run_cmd(session, "update-download-payload", timeout * 8, expect=DOWNLOAD_OK)
    run_cmd(session, "update-prepare", timeout * 8, expect=PREPARE_OK)
    run_cmd(session, "update-apply", timeout * 4, expect=APPLY_OK)
    run_cmd(session, "update-status", timeout, expect=APPLY_SUMMARY)


def assert_attempt_pending(session, timeout: float) -> None:
    run_cmd(
        session,
        "update-rollback-check",
        timeout * 2,
        expect=ATTEMPT_PENDING_SUMMARY,
    )


def confirm_boot_health(session, timeout: float) -> None:
    run_cmd(session, "update-confirm-health", timeout * 4, expect=CONFIRM_OK)
    run_cmd(session, "update-status", timeout, expect=CONFIRM_SUMMARY)


def assert_equal_release_refused(
    session, timeout: float, *, current_version: str
) -> None:
    """Prove that the confirmed release cannot be imported again as an update."""
    run_cmd(
        session,
        "update-fetch",
        timeout * 4,
        expect=MANIFEST_NOT_NEWER_SUMMARY,
    )
    run_cmd(session, "update-status", timeout, expect="rc=-20")
    run_cmd(
        session,
        "update-status",
        timeout,
        expect=f"current={current_version} available=-",
        expect_ignore_line_breaks=True,
    )


def assert_rollback_reported(session, timeout: float) -> None:
    run_cmd(session, "update-rollback-check", timeout * 2, expect=ROLLBACK_OK)
    run_cmd(
        session,
        "update-status",
        timeout,
        expect=ROLLBACK_SUMMARY,
        expect_ignore_line_breaks=True,
    )


def assert_slot_state(session, timeout: float, expect: str) -> None:
    # Slot-status rows wrap at the console width (including in debugcon), so
    # lifecycle tokens must be matched after removing CR/LF boundaries.
    run_cmd(
        session,
        "print-boot-slot",
        timeout,
        expect=expect,
        expect_ignore_line_breaks=True,
    )
