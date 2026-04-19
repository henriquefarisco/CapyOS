#!/usr/bin/env python3
"""Authentication and installer helpers for CapyOS x64 smoke flows."""

from __future__ import annotations

from contextlib import suppress

from smoke_x64_helpers import wait_and_send, wait_for_vm_exit
from smoke_x64_session import SmokeSession


def complete_iso_install(
    session: SmokeSession,
    timeout: float,
    keyboard_layout: str,
    user: str = "admin",
    password: str = "admin",
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

    # Hostname
    mk = session.marker()
    session.wait_for("Hostname [capyos-node]:", timeout=timeout, start_at=mk)
    session.send_line("smoke-node")

    # Theme
    mk = session.marker()
    session.wait_for_any(["Theme [1]:", "Tema [1]:"], timeout=timeout, start_at=mk)
    session.send_line("")

    # Splash
    mk = session.marker()
    session.wait_for_any(
        ["Enable animated splash? [Y/n]:", "Ativar splash animado? [S/n]:",
         "Activar splash animado? [S/n]:"],
        timeout=timeout, start_at=mk,
    )
    session.send_line("n")

    # Admin user
    mk = session.marker()
    session.wait_for_any(
        ["Administrator user [admin]:", "Usuario administrador [admin]:"],
        timeout=timeout, start_at=mk,
    )
    session.send_line("" if user == "admin" else user)

    # Admin password
    mk = session.marker()
    session.wait_for_any(
        ["Password for", "Senha para", "Contrasena para"],
        timeout=timeout, start_at=mk,
    )
    session.send_line(password)

    mk = session.marker()
    session.wait_for_any(
        ["Confirm password:", "Confirme a senha:", "Confirmar contrasena:"],
        timeout=timeout, start_at=mk,
    )
    session.send_line(password)

    # Volume key
    mk = session.marker()
    session.wait_for("Press ENTER to continue...", timeout=timeout, start_at=mk)
    session.send_line("")

    mk = session.marker()
    session.wait_for_any(
        ["Confirm installation? [Y/n]:", "Confirmar instalacao? [S/n]:",
         "Confirmar instalacion? [S/n]:"],
        timeout=timeout, start_at=mk,
    )
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
    volume_key: str | None = None,
) -> None:
    """Wait for first boot to complete.

    When the installer has provisioned all config (hostname, theme, admin,
    etc.), the first boot runs a silent provisioning path and goes straight
    to the login prompt.  If for some reason the interactive wizard appears
    instead, handle it as a fallback.
    """
    mk = session.marker()
    # The silent provisioner prints this, then goes to login.
    # The interactive fallback would show layout prompts instead.
    silent_markers = [
        "Provisionamento automatico",
        "Usuario:",
        "User:",
        "Usuario: ",
        "User: ",
    ]
    volume_key_prompts = [
        "Chave do volume:",
        "Volume key:",
    ]
    runtime_recovery_markers = [
        "[setup] Autocorrecao",
        "Autocorrecao: retentando preparacao do runtime...",
    ]
    legacy_layout_prompts = [
        "Keyboard layout [us]:",
        "Layout do teclado [us]:",
        "Available keyboard layouts:",
        "Layouts de teclado disponiveis:",
    ]
    found = session.wait_for_any(
        volume_key_prompts + runtime_recovery_markers + silent_markers + legacy_layout_prompts,
        timeout=timeout * 4,
        start_at=mk,
    )
    if found in runtime_recovery_markers:
        mk = session.marker()
        found = session.wait_for_any(
            volume_key_prompts + silent_markers + legacy_layout_prompts,
            timeout=timeout * 4,
            start_at=mk,
        )
    if found in volume_key_prompts:
        if not volume_key:
            raise RuntimeError("first boot requested volume key, but no key was provided to the smoke flow")
        session.send_line(volume_key)
        mk = session.marker()
        found = session.wait_for_any(
            silent_markers + legacy_layout_prompts,
            timeout=timeout * 4,
            start_at=mk,
        )
    if found in ("Usuario:", "User:", "Usuario: ", "User: "):
        return
    if found == "Provisionamento automatico":
        # Silent provisioning in progress — just wait for the login prompt
        mk = session.marker()
        session.wait_for_any(
            ["Usuario:", "User:", "Usuario: ", "User: "],
            timeout=timeout * 4,
            start_at=mk,
        )
        return

    # Fallback: interactive wizard (e.g. RAM boot without installer config)
    if found in ("Available keyboard layouts:", "Layouts de teclado disponiveis:"):
        session.send_text("2" if keyboard_layout == "br-abnt2" else "1", newline=False)
    else:
        session.send_line("" if keyboard_layout == "us" else keyboard_layout)
    wait_and_send(session, "Hostname [capyos-node]:", "smoke-node", timeout)
    mk = session.marker()
    theme_prompt = session.wait_for_any(
        [
            "Theme [capyos]:",
            "Tema [capyos]:",
            "Available themes: capyos, ocean, forest.",
            "Temas disponibles: capyos, ocean, forest.",
            "Temas disponiveis: capyos, ocean, forest.",
        ],
        timeout=timeout,
        start_at=mk,
    )
    if theme_prompt.startswith("Theme [") or theme_prompt.startswith("Tema ["):
        session.send_line("capyos")
    else:
        session.send_text("1", newline=False)
    mk = session.marker()
    splash_prompt = session.wait_for_any(
        [
            "Enable animated splash? [Y/n]:",
            "Ativar splash animado? [S/n]:",
            "Activar splash animado? [S/n]:",
            "Animated splash",
            "Splash animado",
        ],
        timeout=timeout,
        start_at=mk,
    )
    if splash_prompt in ("Animated splash", "Splash animado"):
        session.send_text("2", newline=False)
    else:
        session.send_line("n")
    mk = session.marker()
    admin_prompt = session.wait_for_any(
        [
            "Administrator user [admin]:",
            "Usuario administrador [admin]:",
            "Set the password for user",
            "Defina a senha para o usuario",
        ],
        timeout=timeout,
        start_at=mk,
    )
    if admin_prompt in (
        "Administrator user [admin]:",
        "Usuario administrador [admin]:",
    ):
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
        with suppress(TimeoutError):
            outcome = session.wait_for_any(
                [
                    "Passwords do not match.",
                    "As senhas nao coincidem.",
                    "Las contrasenas no coinciden.",
                    "Usuario:",
                    "User:",
                    "Usuario: ",
                    "User: ",
                ],
                timeout=timeout * 2,
                start_at=mk,
            )
            if outcome in ("Usuario:", "User:", "Usuario: ", "User: "):
                break
            continue
        return


def login(
    session: SmokeSession,
    timeout: float,
    user: str,
    password: str,
    allow_desktop: bool = False,
) -> str:
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
    if allow_desktop:
        found = session.wait_for_any(
            [f"{user}@smoke-node>~> ", "[desktop] session started"],
            timeout=timeout,
            start_at=mk,
        )
        return "desktop" if found == "[desktop] session started" else "shell"
    session.wait_for(f"{user}@smoke-node>~> ", timeout=timeout, start_at=mk)
    return "shell"


def assert_shell_identity(session: SmokeSession, timeout: float, user: str) -> None:
    from smoke_x64_helpers import run_cmd

    home = f"/home/{user}"
    run_cmd(session, "print-me", timeout=timeout, expect=user)
    run_cmd(session, "mypath", timeout=timeout, expect=home)
    run_cmd(session, "print-envs", timeout=timeout, expect=f"USER={user}")
    run_cmd(session, "print-envs", timeout=timeout, expect=f"HOME={home}")
    run_cmd(session, "print-envs", timeout=timeout, expect=f"PWD={home}")
