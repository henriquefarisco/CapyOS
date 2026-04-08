#!/usr/bin/env python3
"""Authentication and installer helpers for CapyOS x64 smoke flows."""

from __future__ import annotations

from smoke_x64_helpers import wait_and_send, wait_for_vm_exit
from smoke_x64_session import SmokeSession


def complete_iso_install(
    session: SmokeSession,
    timeout: float,
    keyboard_layout: str,
) -> None:
    mk = session.marker()
    session.wait_for("Press 'I' to start", timeout=timeout * 4, start_at=mk)
    session.send_text("I", newline=False)

    mk = session.marker()
    session.wait_for("Select language [1]:", timeout=timeout, start_at=mk)
    session.send_line("")

    mk = session.marker()
    session.wait_for("Preferred layout [1]:", timeout=timeout, start_at=mk)
    if keyboard_layout == "br-abnt2":
        session.send_line("2")
    else:
        session.send_line("")

    mk = session.marker()
    session.wait_for("Press ENTER to continue...", timeout=timeout, start_at=mk)
    session.send_line("")

    mk = session.marker()
    session.wait_for("Confirm installation? [Y/n]:", timeout=timeout, start_at=mk)
    session.send_line("")

    mk = session.marker()
    session.wait_for(
        "Installation complete. Rebooting...",
        timeout=timeout * 8,
        start_at=mk,
    )
    wait_for_vm_exit(session, timeout=timeout * 2)


def cancel_iso_install(session: SmokeSession, timeout: float) -> None:
    mk = session.marker()
    session.wait_for("Press 'I' to start", timeout=timeout * 4, start_at=mk)
    session.send_text("x", newline=False)


def maybe_run_first_boot_setup(
    session: SmokeSession,
    timeout: float,
    user: str,
    password: str,
    keyboard_layout: str,
) -> None:
    mk = session.marker()
    layout_prompts = [
        "Keyboard layout [us]:",
        "Layout do teclado [us]:",
        "Escolha layout [us]:",
        "Layout del teclado [us]:",
    ]
    found = session.wait_for_any(
        layout_prompts + ["Usuario:", "User:", "Usuario: ", "User: "],
        timeout=timeout * 4,
        start_at=mk,
    )
    if found in ("Usuario:", "User:", "Usuario: ", "User: "):
        return

    session.send_line("" if keyboard_layout == "us" else keyboard_layout)
    wait_and_send(session, "Hostname [capyos-node]:", "smoke-node", timeout)
    mk = session.marker()
    session.wait_for_any(["Theme [capyos]:", "Tema [capyos]:"], timeout=timeout, start_at=mk)
    session.send_line("capyos")
    mk = session.marker()
    session.wait_for_any(
        ["Enable animated splash? [Y/n]:", "Ativar splash animado? [S/n]:"],
        timeout=timeout,
        start_at=mk,
    )
    session.send_line("n")
    mk = session.marker()
    session.wait_for_any(
        ["Administrator user [admin]:", "Usuario administrador [admin]:"],
        timeout=timeout,
        start_at=mk,
    )
    session.send_line("" if user == "admin" else user)
    while True:
        mk = session.marker()
        session.wait_for_any(
            ["Set the password for user", "Defina a senha para o usuario"],
            timeout=timeout,
            start_at=mk,
        )
        session.send_line(password)
        mk = session.marker()
        session.wait_for_any(
            ["Confirm password:", "Confirme a senha:"],
            timeout=timeout,
            start_at=mk,
        )
        session.send_line(password)

        mk = session.marker()
        outcome = session.wait_for_any(
            ["Passwords do not match.", "As senhas nao coincidem.", "Usuario:", "User:"],
            timeout=timeout * 4,
            start_at=mk,
        )
        if outcome in ("Usuario:", "User:"):
            break


def login(session: SmokeSession, timeout: float, user: str, password: str) -> None:
    if "User:" not in session.tail(1600) and "Usuario:" not in session.tail(1600):
        mk = session.marker()
        session.wait_for_any(["Usuario:", "User:"], timeout=timeout, start_at=mk)
    session.send_line(user)
    if "Password:" not in session.tail(1600) and "Senha:" not in session.tail(1600):
        mk = session.marker()
        session.wait_for_any(["Senha:", "Password:"], timeout=timeout, start_at=mk)
    session.send_line(password)
    mk = session.marker()
    session.wait_for_any(
        ["Bem-vindo", "Welcome", "Bienvenido"],
        timeout=timeout,
        start_at=mk,
    )
    session.wait_for(f"{user}@smoke-node>~> ", timeout=timeout, start_at=mk)


def assert_shell_identity(session: SmokeSession, timeout: float, user: str) -> None:
    from smoke_x64_helpers import run_cmd

    home = f"/home/{user}"
    run_cmd(session, "print-me", timeout=timeout, expect=user)
    run_cmd(session, "mypath", timeout=timeout, expect=home)
    run_cmd(session, "print-envs", timeout=timeout, expect=f"USER={user}")
    run_cmd(session, "print-envs", timeout=timeout, expect=f"HOME={home}")
    run_cmd(session, "print-envs", timeout=timeout, expect=f"PWD={home}")
