#!/usr/bin/env python3
"""Authentication and installer helpers for CapyOS x64 smoke flows."""

from __future__ import annotations

import re
from contextlib import suppress

from smoke_x64_helpers import wait_and_send, wait_for_vm_exit
from smoke_x64_session import SmokeSession


def installer_eligible_target_count(text: str) -> int | None:
    prefix = "[installer] eligible-targets="
    values: list[int] = []
    for line in text.splitlines():
        normalized = line.strip()
        if not normalized.startswith(prefix):
            continue
        raw = normalized[len(prefix) :]
        if not raw or not raw.isdecimal():
            return None
        values.append(int(raw, 10))
    if not values or any(value != values[0] for value in values[1:]):
        return None
    return values[0]


def installer_eligible_targets(text: str) -> tuple[tuple[int, str, int], ...]:
    pattern = re.compile(
        r"^\s*\[(\d+)\] PathId ([0-9A-Fa-f]{16}), MediaId \d+ - (\d+) MiB - eligible(?:\s|$)"
    )
    targets: list[tuple[int, str, int]] = []
    for line in text.splitlines():
        match = pattern.match(line)
        if match:
            targets.append(
                (int(match.group(1), 10), match.group(2).lower(), int(match.group(3), 10))
            )
    return tuple(targets)


def installer_select_target_by_size(text: str, size_mib: int) -> tuple[int, str]:
    matches = [target for target in installer_eligible_targets(text) if target[2] == size_mib]
    if len(matches) != 1:
        raise RuntimeError(
            f"installer smoke expected one {size_mib} MiB target, got {len(matches)}"
        )
    return matches[0][0], matches[0][1]


def installer_has_single_eligible_target(text: str) -> bool:
    return installer_eligible_target_count(text) == 1


def require_installer_target_count(text: str, expected: int) -> None:
    actual = installer_eligible_target_count(text)
    if expected < 1 or actual != expected:
        raise RuntimeError(
            f"installer smoke expected {expected} eligible targets, got {actual}"
        )


def require_first_boot_wizard(marker: str, required: bool) -> None:
    direct_login = marker in ("Usuario:", "User:", "Usuario: ", "User: ")
    if required and (direct_login or marker == "Provisionamento automatico"):
        raise RuntimeError("fresh install reached login without first-boot wizard")


MODULE_INSTALL_DONE_MARKERS = [
    "Instalacao concluida. Reinicie para ativar.",
    "Install complete. Reboot to activate.",
    "Instalacion completa. Reinicie para activar.",
]
MODULE_INSTALL_BACKGROUND_MARKERS = [
    "capypkg nao iniciou; seguira em segundo plano.",
    "capypkg not started; will continue in background.",
    "capypkg no inicio; seguira en segundo plano.",
]
MODULE_REBOOT_MARKERS = [
    "Configuracao inicial concluida. Reiniciando para ativar modulos...",
    "Initial setup complete. Rebooting to activate modules...",
    "Configuracion inicial completa. Reiniciando para activar modulos...",
]
LOGIN_PROMPTS = ["Usuario:", "User:", "Usuario: ", "User: "]


def module_install_completed(text: str) -> bool:
    return any(marker in text for marker in MODULE_INSTALL_DONE_MARKERS)


def complete_iso_install(
    session: SmokeSession,
    timeout: float,
    keyboard_layout: str,
    user: str = "admin",
    password: str = "admin",
    expected_eligible_targets: int = 1,
    target_selection: int = 1,
    target_size_mib: int | None = None,
) -> None:
    mk = session.marker()
    entry_prompt = session.wait_for_any(
        ["Select target disk [", "Press 'I' to start"],
        timeout=timeout * 4,
        start_at=mk,
    )
    if entry_prompt != "Select target disk [":
        raise RuntimeError("installer lacks mandatory explicit target selection")
    require_installer_target_count(session.tail(4000), expected_eligible_targets)
    selected_path_id = ""
    if target_size_mib is not None:
        target_selection, selected_path_id = installer_select_target_by_size(
            session.tail(4000), target_size_mib
        )
    if target_selection < 1 or target_selection > expected_eligible_targets:
        raise RuntimeError("installer target selection is outside eligible range")
    mk = session.marker()
    session.send_line(str(target_selection))
    session.wait_for("Press 'I' to start", timeout=timeout, start_at=mk)
    if (
        selected_path_id
        and f"selected pathid {selected_path_id}" not in session.text_since(mk).lower()
    ):
        raise RuntimeError("installer selected a different PathId than requested target")
    mk = session.marker()
    session.send_text("I", newline=False)
    installer_prompt = session.wait_for_any(
        [
            "Select language [1]:",
            "=== Volume Recovery Key ===",
            "Type ERASE and press ENTER to confirm:",
            "Confirm installation? [Y/n]:",
            "Confirmar instalacao? [S/n]:",
            "Confirmar instalacion? [S/n]:",
        ],
        timeout=timeout,
        start_at=mk,
    )
    if installer_prompt != "Select language [1]:":
        if installer_prompt == "=== Volume Recovery Key ===":
            confirm_prompts = [
                "Type ERASE and press ENTER to confirm:",
                "Confirm installation? [Y/n]:",
                "Confirmar instalacao? [S/n]:",
                "Confirmar instalacion? [S/n]:",
            ]
            confirm_prompt = next(
                (prompt for prompt in confirm_prompts if prompt in session.tail(2400)),
                "",
            )
            if not confirm_prompt:
                mk = session.marker()
                confirm_prompt = session.wait_for_any(
                    confirm_prompts, timeout=timeout, start_at=mk
                )
        else:
            confirm_prompt = installer_prompt
        mk = session.marker()
        session.send_line(
            "ERASE"
            if confirm_prompt == "Type ERASE and press ENTER to confirm:"
            else ""
        )
        session.wait_for(
            "Installation complete. Rebooting...",
            timeout=timeout * 8,
            start_at=mk,
        )
        wait_for_vm_exit(session, timeout=timeout * 2)
        return

    mk = session.marker()
    session.send_line("")
    session.wait_for("Preferred layout [1]:", timeout=timeout, start_at=mk)
    mk = session.marker()
    if keyboard_layout == "br-abnt2":
        session.send_line("2")
    else:
        session.send_line("")

    # Hostname
    session.wait_for("Hostname [capyos-node]:", timeout=timeout, start_at=mk)
    mk = session.marker()
    session.send_line("smoke-node")

    # Theme
    session.wait_for_any(["Theme [1]:", "Tema [1]:"], timeout=timeout, start_at=mk)
    mk = session.marker()
    session.send_line("")

    # Splash
    session.wait_for_any(
        ["Enable animated splash? [Y/n]:", "Ativar splash animado? [S/n]:",
         "Activar splash animado? [S/n]:"],
        timeout=timeout, start_at=mk,
    )
    mk = session.marker()
    session.send_line("n")

    # Admin user
    session.wait_for_any(
        ["Administrator user [admin]:", "Usuario administrador [admin]:"],
        timeout=timeout, start_at=mk,
    )
    mk = session.marker()
    session.send_line("" if user == "admin" else user)

    # Admin password
    session.wait_for_any(
        ["Password for", "Senha para", "Contrasena para"],
        timeout=timeout, start_at=mk,
    )
    mk = session.marker()
    session.send_line(password)

    session.wait_for_any(
        ["Confirm password:", "Confirme a senha:", "Confirmar contrasena:"],
        timeout=timeout, start_at=mk,
    )
    mk = session.marker()
    session.send_line(password)

    # Volume key
    session.wait_for("Press ENTER to continue...", timeout=timeout, start_at=mk)
    mk = session.marker()
    session.send_line("")

    confirm_prompt = session.wait_for_any(
        [
            "Type ERASE and press ENTER to confirm:",
            "Confirm installation? [Y/n]:",
            "Confirmar instalacao? [S/n]:",
            "Confirmar instalacion? [S/n]:",
        ],
        timeout=timeout,
        start_at=mk,
    )
    mk = session.marker()
    session.send_line(
        "ERASE"
        if confirm_prompt == "Type ERASE and press ENTER to confirm:"
        else ""
    )
    session.wait_for(
        "Installation complete. Rebooting...",
        timeout=timeout * 8,
        start_at=mk,
    )
    wait_for_vm_exit(session, timeout=timeout * 2)


def cancel_iso_install(session: SmokeSession, timeout: float) -> None:
    mk = session.marker()
    prompt = session.wait_for_any(
        ["Select target disk [", "Press 'I' to start"],
        timeout=timeout * 4,
        start_at=mk,
    )
    if prompt == "Select target disk [":
        session.send_line("0")
    else:
        session.send_text("x", newline=False)


def maybe_run_first_boot_setup(
    session: SmokeSession,
    timeout: float,
    user: str,
    password: str,
    keyboard_layout: str,
    volume_key: str | None = None,
    module_profile: str = "basic",
    modules_index_url: str = "",
    require_interactive: bool = False,
) -> str:
    """Complete first boot, optionally requiring the interactive wizard."""
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
    require_first_boot_wizard(found, require_interactive)
    if found in ("Usuario:", "User:", "Usuario: ", "User: "):
        return "login"
    if found == "Provisionamento automatico":
        # Silent provisioning in progress — just wait for the login prompt
        mk = session.marker()
        session.wait_for_any(
            ["Usuario:", "User:", "Usuario: ", "User: "],
            timeout=timeout * 4,
            start_at=mk,
        )
        return "login"

    # Fallback: interactive wizard (e.g. RAM boot without installer config)
    if found in ("Available keyboard layouts:", "Layouts de teclado disponiveis:"):
        session.send_text("2" if keyboard_layout == "br-abnt2" else "1", newline=False)
    else:
        session.send_line("" if keyboard_layout == "us" else keyboard_layout)
    wait_and_send(session, "Hostname [capyos-node]:", "smoke-node", timeout)
    mk = session.marker()
    theme_prompts = [
        "Theme [capyos]:",
        "Tema [capyos]:",
        "Available themes: capyos, ocean, forest.",
        "Available themes: capyos, ocean, forest, love.",
        "Temas disponibles: capyos, ocean, forest.",
        "Temas disponibles: capyos, ocean, forest, love.",
        "Temas disponiveis: capyos, ocean, forest.",
        "Temas disponiveis: capyos, ocean, forest, love.",
    ]
    splash_prompts = [
        "Enable animated splash? [Y/n]:",
        "Ativar splash animado? [S/n]:",
        "Activar splash animado? [S/n]:",
        "Animated splash",
        "Splash animado",
    ]
    prompt = session.wait_for_any(
        theme_prompts + splash_prompts,
        timeout=timeout,
        start_at=mk,
    )
    if prompt in theme_prompts:
        if prompt.startswith("Theme [") or prompt.startswith("Tema ["):
            session.send_line("capyos")
        else:
            session.send_text("1", newline=False)
        mk = session.marker()
        splash_prompt = session.wait_for_any(
            splash_prompts,
            timeout=timeout,
            start_at=mk,
        )
    else:
        splash_prompt = prompt
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
                    "Installation profile",
                    "Perfil de instalacao",
                    "Perfil de instalacion",
                ],
                timeout=timeout * 2,
                start_at=mk,
            )
            if outcome in ("Usuario:", "User:", "Usuario: ", "User: "):
                break
            if outcome in (
                "Installation profile",
                "Perfil de instalacao",
                "Perfil de instalacion",
            ):
                profile_keys = {"basic": "1", "full": "2", "custom": "3"}
                session.send_text(profile_keys[module_profile], newline=False)
                if module_profile in ("full", "custom"):
                    mk = session.marker()
                    session.wait_for_any(
                        [
                            "Modules-index URL",
                            "URL do indice de modulos",
                            "URL del indice de modulos",
                        ],
                        timeout=timeout,
                        start_at=mk,
                    )
                    session.send_line(modules_index_url)
                    if module_profile == "custom":
                        mk = session.marker()
                        session.wait_for_any(
                            [
                                "Official modules",
                                "Modulos oficiais",
                                "Modulos oficiales",
                            ],
                            timeout=timeout,
                            start_at=mk,
                        )
                        session.send_line("")
                    mk = session.marker()
                    module_outcome = session.wait_for_any(
                        MODULE_INSTALL_DONE_MARKERS
                        + MODULE_INSTALL_BACKGROUND_MARKERS
                        + MODULE_REBOOT_MARKERS
                        + LOGIN_PROMPTS,
                        timeout=timeout * 10,
                        start_at=mk,
                    )
                    if module_outcome in MODULE_REBOOT_MARKERS:
                        wait_for_vm_exit(session, timeout=timeout * 2)
                        return "rebooted"
                    if module_outcome in LOGIN_PROMPTS:
                        break
                    found_after_modules = session.wait_for_any(
                        MODULE_REBOOT_MARKERS + LOGIN_PROMPTS,
                        timeout=timeout * 8,
                        start_at=mk,
                    )
                    if found_after_modules in MODULE_REBOOT_MARKERS:
                        wait_for_vm_exit(session, timeout=timeout * 2)
                        return "rebooted"
                    break
                mk = session.marker()
                session.wait_for_any(
                    ["Usuario:", "User:", "Usuario: ", "User: "],
                    timeout=timeout * 4,
                    start_at=mk,
                )
                break
            continue
        return "login"
    return "login"


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
            [
                f"{user}@smoke-node>~> ",
                f"{user}@capyos64>~> ",
                f"{user}@capyos-node>~> ",
                "[smoke] mouse-events ready",
                "[smoke] gui-session ready",
                "[desktop] session started",
            ],
            timeout=timeout,
            start_at=mk,
        )
        return "desktop" if found in (
            "[smoke] mouse-events ready",
            "[smoke] gui-session ready",
            "[desktop] session started",
        ) else "shell"
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
