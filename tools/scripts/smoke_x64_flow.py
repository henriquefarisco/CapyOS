#!/usr/bin/env python3
"""Smoke flow helpers for CapyOS x64 CLI validation."""

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


def trigger_reboot(session: SmokeSession, timeout: float) -> bool:
    reboot_markers = ("Reiniciando...", "Rebooting...")
    for _ in range(4):
        mk = session.marker()
        session.send_line("shutdown-reboot")
        try:
            session.wait_for_any(reboot_markers, timeout=8.0, start_at=mk)
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


def trigger_poweroff(session: SmokeSession, timeout: float) -> bool:
    poweroff_markers = ("Desligando...", "Powering off...", "Apagando...")
    for _ in range(4):
        mk = session.marker()
        session.send_line("shutdown-off")
        try:
            session.wait_for_any(poweroff_markers, timeout=8.0, start_at=mk)
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

    # first-boot wizard path
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
    home = f"/home/{user}"
    run_cmd(session, "print-me", timeout=timeout, expect=user)
    run_cmd(session, "mypath", timeout=timeout, expect=home)
    run_cmd(session, "print-envs", timeout=timeout, expect=f"USER={user}")
    run_cmd(session, "print-envs", timeout=timeout, expect=f"HOME={home}")
    run_cmd(session, "print-envs", timeout=timeout, expect=f"PWD={home}")


def smoke_first_boot(
    session: SmokeSession, timeout: float, user: str, password: str, marker: str
) -> None:
    deep_home = f"/home/{user}/docs/projetos/capy"
    assert_shell_identity(session, timeout=timeout, user=user)
    run_cmd(session, "config-theme show", timeout=timeout, expect="Current theme: capyos")
    run_cmd(session, "config-theme ocean", timeout=timeout, expect="theme updated")
    run_cmd(session, "config-splash show", timeout=timeout, expect="Current splash: disabled")
    run_cmd(
        session,
        "config-splash on",
        timeout=timeout,
        expect="splash enabled for the next boot",
    )
    run_cmd(session, "config-language show", timeout=timeout, expect="Current language: en")
    run_cmd(session, "print-envs", timeout=timeout, expect="LANG=en")
    run_cmd(
        session,
        "mk-dir -help",
        timeout=timeout,
        expect="Creates a directory (and needed parents) in the given path.",
    )
    run_cmd(
        session,
        "print-file -help",
        timeout=timeout,
        expect="Shows the full content of a text file.",
    )
    run_cmd(
        session,
        "add-user -help",
        timeout=timeout,
        expect="Creates a new local user. Accepted roles: user, admin.",
    )
    run_cmd(
        session,
        "net-status -help",
        timeout=timeout,
        expect="Shows network state: active driver, IPv4, gateway, ARP and counters.",
        expect_optional=True,
    )
    run_cmd(
        session,
        "add-user smokeuser smoke user",
        timeout=timeout,
        expect="user=smokeuser",
    )
    mk = session.marker()
    session.send_line("bye")
    session.wait_for("Logging out", timeout=timeout, start_at=mk)
    login(session=session, timeout=timeout, user="smokeuser", password="smoke")
    assert_shell_identity(session, timeout=timeout, user="smokeuser")
    run_cmd(session, "config-language show", timeout=timeout, expect="Current language: en")
    run_cmd(session, "print-envs", timeout=timeout, expect="LANG=en")
    mk = session.marker()
    session.send_line("bye")
    session.wait_for("Logging out", timeout=timeout, start_at=mk)
    login(session=session, timeout=timeout, user=user, password=password)
    assert_shell_identity(session, timeout=timeout, user=user)
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="theme=ocean")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="splash=enabled")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="language=en")
    run_cmd(session, f"mk-dir {deep_home}", timeout=timeout, expect="[ok]")
    run_cmd_expect_prompt(
        session,
        f"go {deep_home}",
        timeout=timeout,
        prompt=f"{user}@smoke-node>~/.../projetos/capy> ",
        expect="[ok]",
    )
    run_cmd(session, "mypath", timeout=timeout, expect=deep_home)
    run_cmd_expect_prompt(
        session,
        f"go /home/{user}",
        timeout=timeout,
        prompt=f"{user}@smoke-node>~> ",
        expect="[ok]",
    )
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
    if not trigger_reboot(session, timeout=timeout * 2):
        raise RuntimeError("shutdown-reboot did not terminate the VM")


def smoke_second_boot(
    session: SmokeSession, timeout: float, user: str, password: str, marker: str
) -> None:
    smoke_file = "/tmp/smoke-persist/smoke.txt"
    login(session, timeout=timeout, user=user, password=password)
    assert_shell_identity(session, timeout=timeout, user=user)
    run_cmd(session, "config-language show", timeout=timeout, expect="Current language: en")
    run_cmd(session, "print-envs", timeout=timeout, expect="LANG=en")
    run_cmd(session, "config-theme show", timeout=timeout, expect="Current theme: ocean")
    run_cmd(session, "config-splash show", timeout=timeout, expect="Current splash: enabled")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="theme=ocean")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="splash=enabled")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="language=en")
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
    run_cmd(session, "list-users", timeout=timeout, expect="smokeuser")

    mk = session.marker()
    session.send_line("bye")
    session.wait_for("Logging out", timeout=timeout, start_at=mk)
    login(session=session, timeout=timeout, user="smokeuser", password="smoke")
    assert_shell_identity(session, timeout=timeout, user="smokeuser")
    run_cmd(session, "config-language show", timeout=timeout, expect="Current language: en")
    if not trigger_poweroff(session, timeout=timeout * 2):
        raise RuntimeError("shutdown-off did not terminate the VM")
