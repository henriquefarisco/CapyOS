#!/usr/bin/env python3
"""Smoke flow helpers for NoirOS x64 CLI validation."""

from __future__ import annotations

import time

from smoke_x64_session import SmokeSession


def run_cmd(
    session: SmokeSession,
    cmd: str,
    timeout: float,
    expect: str | None = None,
    expect_optional: bool = False,
) -> None:
    mk = session.marker()
    session.send_line(cmd)
    if expect:
        try:
            session.wait_for(expect, timeout=timeout, start_at=mk)
        except TimeoutError:
            if not expect_optional:
                raise
    session.wait_for("> ", timeout=timeout, start_at=mk)


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
    session.wait_for("arquivo salvo", timeout=timeout, start_at=mk)
    session.wait_for("> ", timeout=timeout, start_at=mk)


def wait_for_vm_exit(session: SmokeSession, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if session.proc is not None and session.proc.poll() is not None:
            return
        time.sleep(0.1)
    raise TimeoutError("timeout waiting VM process to exit")


def trigger_reboot(session: SmokeSession, timeout: float) -> bool:
    shutdown_attempts = (
        ("shutdown-off", "Desligando..."),
        ("shutdown-reboot", "Reiniciando..."),
    )
    for cmd, marker in shutdown_attempts:
        for _ in range(4):
            mk = session.marker()
            session.send_line(cmd)
            try:
                session.wait_for(marker, timeout=8.0, start_at=mk)
                wait_for_vm_exit(session, timeout=timeout)
                return True
            except RuntimeError as exc:
                if "code 0" in str(exc):
                    return True
                raise
            except TimeoutError:
                try:
                    session.wait_for("> ", timeout=8.0, start_at=mk)
                except TimeoutError:
                    pass
    return False


def wait_and_send(session: SmokeSession, pattern: str, value: str, timeout: float) -> None:
    if pattern not in session.tail(1200):
        mk = session.marker()
        session.wait_for(pattern, timeout=timeout, start_at=mk)
    session.send_line(value)


def maybe_run_first_boot_setup(
    session: SmokeSession,
    timeout: float,
    user: str,
    password: str,
    keyboard_layout: str,
) -> None:
    mk = session.marker()
    found = session.wait_for_any(
        ["Layout do teclado [us]:", "Usuario:"],
        timeout=timeout * 4,
        start_at=mk,
    )
    if found == "Usuario:":
        return

    # first-boot wizard path
    session.send_line("" if keyboard_layout == "us" else keyboard_layout)
    wait_and_send(session, "Hostname [capyos-node]:", "smoke-node", timeout)
    wait_and_send(session, "Tema [noir]:", "noir", timeout)
    wait_and_send(session, "Ativar splash animado? [S/n]:", "n", timeout)
    wait_and_send(session, "Usuario administrador [admin]:", user, timeout)
    while True:
        wait_and_send(session, "Defina a senha para o usuario", password, timeout)
        wait_and_send(session, "Confirme a senha:", password, timeout)

        mk = session.marker()
        outcome = session.wait_for_any(
            ["As senhas nao coincidem.", "Usuario:"],
            timeout=timeout * 4,
            start_at=mk,
        )
        if outcome == "Usuario:":
            break


def login(session: SmokeSession, timeout: float, user: str, password: str) -> None:
    wait_and_send(session, "Usuario:", user, timeout)
    wait_and_send(session, "Senha:", password, timeout)
    mk = session.marker()
    session.wait_for("Bem-vindo", timeout=timeout, start_at=mk)
    session.wait_for("> ", timeout=timeout, start_at=mk)


def smoke_first_boot(session: SmokeSession, timeout: float, user: str, marker: str) -> None:
    run_cmd(session, "mypath", timeout=timeout, expect="/")
    smoke_dir = "/tmp/smoke-persist"
    smoke_file = "smoke.txt"
    run_cmd(session, f"mk-dir {smoke_dir}", timeout=timeout, expect="[ok]")
    run_cmd(session, f"go {smoke_dir}", timeout=timeout)
    run_cmd(session, f"mk-file {smoke_file}", timeout=timeout, expect="[ok]")
    run_open_write(session, smoke_file, [f"marker:{marker}", "linha-2"], timeout=timeout)
    run_cmd(session, f"print-file {smoke_file}", timeout=timeout, expect=f"marker:{marker}")
    run_cmd(session, "go /", timeout=timeout)
    run_cmd(
        session,
        "do-sync",
        timeout=timeout,
        expect="buffers sincronizados",
        expect_optional=True,
    )
    _ = trigger_reboot(session, timeout=timeout * 2)


def smoke_second_boot(
    session: SmokeSession, timeout: float, user: str, password: str, marker: str
) -> None:
    smoke_file = "/tmp/smoke-persist/smoke.txt"
    login(session, timeout=timeout, user=user, password=password)
    marker_expect = f"marker:{marker}"
    marker_ok = False
    last_error: Exception | None = None
    for attempt in range(3):
        try:
            if attempt == 0:
                run_cmd(session, f"print-file {smoke_file}", timeout=timeout, expect=marker_expect)
            else:
                run_cmd(session, "go /tmp/smoke-persist", timeout=timeout)
                run_cmd(session, "print-file smoke.txt", timeout=timeout, expect=marker_expect)
                run_cmd(session, "go /", timeout=timeout)
            marker_ok = True
            break
        except (TimeoutError, RuntimeError) as exc:
            last_error = exc
    if not marker_ok:
        if last_error:
            raise last_error
        raise RuntimeError("persistence marker validation failed")
    run_cmd(session, "list-users", timeout=timeout, expect=user)

    mk = session.marker()
    session.send_line("bye")
    session.wait_for("Encerrando sessao", timeout=timeout, start_at=mk)
    session.wait_for("Usuario:", timeout=timeout, start_at=mk)
