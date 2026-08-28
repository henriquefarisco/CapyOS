#!/usr/bin/env python3
"""Shared helpers for CapyOS x64 smoke flows."""

from __future__ import annotations

import re
import time

from smoke_x64_session import SmokeSession, text_contains_pattern

SHELL_PROMPT_TOKEN = "\0CAPYOS_SHELL_PROMPT\0"
SHELL_PROMPT_RE = re.compile(
    r"(?:^|[\r\n])[^\s@>]+@[^\s@>]+>[^\r\n]*> "
)


def _contains_shell_prompt(text: str) -> bool:
    return SHELL_PROMPT_RE.search(text) is not None


def _primary_text_since(session: SmokeSession, start_at: object) -> str:
    serial_text_since = getattr(session, "serial_text_since", None)
    if callable(serial_text_since):
        return str(serial_text_since(start_at))
    return session.text_since(start_at)


def _wait_for_primary_any(
    session: SmokeSession,
    patterns: list[str],
    *,
    timeout: float,
    start_at: object,
    ignore_line_breaks: bool = False,
) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = _primary_text_since(session, start_at)
        for pattern in patterns:
            if pattern == SHELL_PROMPT_TOKEN:
                matched = _contains_shell_prompt(text)
            else:
                matched = text_contains_pattern(
                    text, pattern, ignore_line_breaks=ignore_line_breaks
                )
            if matched:
                return pattern
        proc = getattr(session, "proc", None)
        if proc is not None and proc.poll() is not None:
            raise RuntimeError(f"VM exited early with code {proc.returncode}")
        time.sleep(0.05)

    text = _primary_text_since(session, start_at)
    for pattern in patterns:
        if pattern == SHELL_PROMPT_TOKEN:
            matched = _contains_shell_prompt(text)
        else:
            matched = text_contains_pattern(
                text, pattern, ignore_line_breaks=ignore_line_breaks
            )
        if matched:
            return pattern
    raise TimeoutError(f"timeout waiting for patterns: {patterns!r}")


def run_cmd(
    session: SmokeSession,
    cmd: str,
    timeout: float,
    expect: str | list[str] | tuple[str, ...] | None = None,
    expect_optional: bool = False,
    expect_ignore_line_breaks: bool = False,
) -> None:
    mk = session.marker()
    session.send_line(cmd)
    if expect:
        try:
            if isinstance(expect, (list, tuple)):
                patterns = list(expect)
                patterns.append(SHELL_PROMPT_TOKEN)
                found = _wait_for_primary_any(
                    session,
                    patterns,
                    timeout=timeout,
                    start_at=mk,
                    ignore_line_breaks=expect_ignore_line_breaks,
                )
                if found == SHELL_PROMPT_TOKEN and not expect_optional:
                    raise RuntimeError(
                        f"command {cmd!r} returned to the shell before "
                        f"expected output {list(expect)!r}"
                    )
            elif expect_optional:
                _wait_for_primary_any(
                    session,
                    [expect, SHELL_PROMPT_TOKEN],
                    timeout=timeout,
                    start_at=mk,
                    ignore_line_breaks=expect_ignore_line_breaks,
                )
            elif expect_ignore_line_breaks:
                session.wait_for(
                    expect,
                    timeout=timeout,
                    start_at=mk,
                    ignore_line_breaks=True,
                )
            else:
                found = _wait_for_primary_any(
                    session,
                    [expect, SHELL_PROMPT_TOKEN],
                    timeout=timeout,
                    start_at=mk,
                    ignore_line_breaks=expect_ignore_line_breaks,
                )
                if found == SHELL_PROMPT_TOKEN:
                    raise RuntimeError(
                        f"command {cmd!r} returned to the shell before "
                        f"expected output {expect!r}"
                    )
        except TimeoutError:
            if not expect_optional:
                raise
            if _contains_shell_prompt(_primary_text_since(session, mk)):
                return
    if _contains_shell_prompt(_primary_text_since(session, mk)):
        return
    _wait_for_primary_any(
        session, [SHELL_PROMPT_TOKEN], timeout=timeout, start_at=mk
    )


def run_cmd_expect_prompt(
    session: SmokeSession,
    cmd: str,
    timeout: float,
    prompt: str,
    expect: str | None = None,
) -> None:
    mk = session.marker()
    session.send_line(cmd)
    if expect:
        session.wait_for(expect, timeout=timeout, start_at=mk)
    session.wait_for(prompt, timeout=timeout, start_at=mk)


def ensure_shell_after_login(session: SmokeSession, timeout: float, mode: str) -> str:
    if mode == "shell":
        return mode
    print("[info] desktop autostart detected; exiting to CLI for smoke commands")
    mk = session.marker()
    session.send_byte(0x9D)
    session.wait_for("[desktop] session stopped", timeout=timeout, start_at=mk)
    session.wait_for(">~> ", timeout=timeout, start_at=mk)
    return "shell"


def run_open_write(
    session: SmokeSession, filename: str, lines: list[str], timeout: float
) -> None:
    mk = session.marker()
    session.send_line(f"open {filename}")
    session.wait_for("open> ", timeout=timeout, start_at=mk)
    for line in lines:
        session.send_line(line)
        session.wait_for("open> ", timeout=timeout, start_at=mk)
    session.send_line(".wq")
    try:
        session.wait_for("file saved", timeout=timeout, start_at=mk)
    except TimeoutError:
        session.wait_for("arquivo salvo", timeout=timeout, start_at=mk)
    session.wait_for("> ", timeout=timeout, start_at=mk)


def wait_for_vm_exit(session: SmokeSession, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if session.proc is not None and session.proc.poll() is not None:
            return
        time.sleep(0.1)
    raise TimeoutError("timeout waiting VM process to exit")


def vm_has_exited(session: SmokeSession) -> bool:
    return session.proc is not None and session.proc.poll() is not None


def trigger_reboot(session: SmokeSession, timeout: float) -> bool:
    reboot_markers = ("Reiniciando...", "Rebooting...")
    for _ in range(4):
        if vm_has_exited(session):
            return True
        mk = session.marker()
        try:
            session.send_line("shutdown-reboot")
        except OSError:
            return vm_has_exited(session) or any(marker in session.tail() for marker in reboot_markers)
        try:
            session.wait_for_any(reboot_markers, timeout=8.0, start_at=mk)
            return True
        except RuntimeError as exc:
            if "code 0" in str(exc):
                return True
            raise
        except OSError:
            return vm_has_exited(session) or any(marker in session.tail() for marker in reboot_markers)
        except TimeoutError:
            if vm_has_exited(session):
                return True
            try:
                session.wait_for("> ", timeout=8.0, start_at=mk)
            except TimeoutError:
                if vm_has_exited(session):
                    return True
                pass
    return False


def trigger_poweroff(session: SmokeSession, timeout: float) -> bool:
    poweroff_markers = ("Desligando...", "Powering off...", "Apagando...")
    for _ in range(4):
        if vm_has_exited(session):
            return True
        mk = session.marker()
        try:
            session.send_line("shutdown-off")
        except OSError:
            return vm_has_exited(session) or any(marker in session.tail() for marker in poweroff_markers)
        try:
            session.wait_for_any(poweroff_markers, timeout=8.0, start_at=mk)
            try:
                wait_for_vm_exit(session, timeout=timeout)
            except TimeoutError:
                if any(marker in session.tail() for marker in poweroff_markers):
                    return True
                raise
            return True
        except RuntimeError as exc:
            if "code 0" in str(exc):
                return True
            raise
        except OSError:
            return vm_has_exited(session) or any(marker in session.tail() for marker in poweroff_markers)
        except TimeoutError:
            if vm_has_exited(session):
                return True
            try:
                session.wait_for("> ", timeout=8.0, start_at=mk)
            except TimeoutError:
                if vm_has_exited(session):
                    return True
                pass
    return False


def wait_and_send(session: SmokeSession, pattern: str, value: str, timeout: float) -> None:
    if pattern not in session.tail(1200):
        mk = session.marker()
        session.wait_for(pattern, timeout=timeout, start_at=mk)
    session.send_line(value)
