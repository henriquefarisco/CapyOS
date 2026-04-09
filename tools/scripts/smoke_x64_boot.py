#!/usr/bin/env python3
"""Boot-stage validation flows for CapyOS x64 smoke tests."""

from __future__ import annotations

from smoke_x64_auth import assert_shell_identity, login
from smoke_x64_helpers import (
    run_cmd,
    run_cmd_expect_prompt,
    run_open_write,
    trigger_poweroff,
    trigger_reboot,
)
from smoke_x64_session import SmokeSession


def smoke_first_boot(
    session: SmokeSession, timeout: float, user: str, password: str, marker: str
) -> None:
    deep_home = f"/home/{user}/docs/projetos/capy"
    update_manifest = "/tmp/update-manifest.ini"
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
    run_cmd(session, "job-status recovery-snapshot", timeout=timeout, expect="recovery-snapshot state=")
    run_cmd(session, "update-status", timeout=timeout, expect="channel=stable")
    run_cmd(session, "update-channel show", timeout=timeout, expect="channel=stable branch=main")
    run_cmd(session, "update-channel develop", timeout=timeout, expect="channel=develop branch=develop")
    run_cmd(session, "update-status", timeout=timeout, expect="channel=develop")
    run_cmd(session, "update-status", timeout=timeout, expect="branch=develop")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="update_channel=develop")
    run_cmd(session, "print-file /system/update/repository.ini", timeout=timeout, expect="branch=develop")
    run_cmd(session, "update-channel stable", timeout=timeout, expect="channel=stable branch=main")
    run_cmd(session, "update-check", timeout=timeout, expect="catalog cache missing")
    run_cmd(
        session,
        "print-file /system/update/repository.ini",
        timeout=timeout,
        expect="source=github:henriquefarisco/CapyOS",
    )
    run_cmd(
        session,
        "update-channel show",
        timeout=timeout,
        expect="remote=https://raw.githubusercontent.com/henriquefarisco/CapyOS/main",
    )
    run_open_write(
        session,
        update_manifest,
        [
            "available_version=0.8.1-alpha.0+20260409",
            "channel=stable",
            "branch=main",
            "source=github:henriquefarisco/CapyOS",
            "published_at=2026-04-09",
        ],
        timeout=timeout,
    )
    run_cmd(
        session,
        f"update-import-manifest {update_manifest}",
        timeout=timeout,
        expect="manifest imported into the local catalog",
    )
    run_cmd(session, "update-status", timeout=timeout, expect="catalog=present")
    run_cmd(
        session,
        "update-status",
        timeout=timeout,
        expect="available=0.8.1-alpha.0+20260409",
    )
    run_cmd(
        session,
        "job-run recovery-snapshot",
        timeout=timeout,
        expect="job scheduled for immediate execution",
    )
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
        expect="Shows network state: mode, detected driver, runtime readiness, IPv4, gateway, ARP and counters.",
        expect_optional=True,
    )
    run_cmd(
        session,
        "net-refresh -help",
        timeout=timeout,
        expect="Refreshes network runtime diagnostics; on Hyper-V, queries the NetVSC offer outside boot.",
        expect_optional=True,
    )
    run_cmd(
        session,
        "net-dump-runtime -help",
        timeout=timeout,
        expect="Shows a detailed dump of the network runtime, including gate, phase, refresh counters and Hyper-V/StorVSC state.",
        expect_optional=True,
    )
    run_cmd(
        session,
        "runtime-native -help",
        timeout=timeout,
        expect="Shows the native-runtime gate and, with 'step', executes one controlled step of the Hyper-V coordinator.",
        expect_optional=True,
    )
    run_cmd(
        session,
        "net-resolve -help",
        timeout=timeout,
        expect="Resolves a hostname through DNS using the currently configured server.",
        expect_optional=True,
    )
    run_cmd(session, "net-status", timeout=timeout, expect="runtime=ready", expect_optional=True)
    run_cmd(
        session,
        "net-dump-runtime",
        timeout=timeout,
        expect="runtime.initialized=yes",
        expect_optional=True,
    )
    run_cmd(
        session,
        "runtime-native show",
        timeout=timeout,
        expect="platform runtime:",
        expect_optional=True,
    )
    run_cmd(session, "net-refresh", timeout=timeout, expect="driver=", expect_optional=True)
    run_cmd(
        session,
        "net-set 10.0.2.42 255.255.255.0 10.0.2.2 1.1.1.1",
        timeout=timeout,
        expect="network configuration applied",
        expect_optional=True,
    )
    run_cmd(
        session,
        "net-mode dhcp",
        timeout=timeout,
        expect="network mode changed to dhcp",
        expect_optional=True,
    )
    run_cmd(session, "net-mode show", timeout=timeout, expect="dhcp", expect_optional=True)
    run_cmd(
        session,
        "net-resolve example.com",
        timeout=timeout,
        expect="name=example.com ipv4=",
        expect_optional=True,
    )
    run_cmd(
        session,
        "hey gateway",
        timeout=timeout,
        expect="hello from (",
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
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="update_channel=stable")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="network_mode=dhcp")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="ipv4=10.0.2.42")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="mask=255.255.255.0")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="gateway=10.0.2.2")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="dns=1.1.1.1")
    run_cmd(
        session,
        "print-file /system/update/repository.ini",
        timeout=timeout,
        expect="branch=main",
    )
    run_cmd(
        session,
        "print-file /system/update/repository.ini",
        timeout=timeout,
        expect="manifest=/system/update/latest.ini",
    )
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
    run_cmd(session, "job-status recovery-snapshot", timeout=timeout, expect="recovery-snapshot state=")
    run_cmd(session, "update-status", timeout=timeout, expect="channel=stable")
    run_cmd(session, "update-status", timeout=timeout, expect="branch=main")
    run_cmd(session, "update-status", timeout=timeout, expect="catalog=present")
    run_cmd(
        session,
        "update-status",
        timeout=timeout,
        expect="available=0.8.1-alpha.0+20260409",
    )
    run_cmd(session, "update-channel show", timeout=timeout, expect="channel=stable branch=main")
    run_cmd(session, "net-mode show", timeout=timeout, expect="dhcp", expect_optional=True)
    run_cmd(session, "net-refresh", timeout=timeout, expect="driver=", expect_optional=True)
    run_cmd(
        session,
        "net-dump-runtime",
        timeout=timeout,
        expect="runtime.initialized=yes",
        expect_optional=True,
    )
    run_cmd(
        session,
        "runtime-native show",
        timeout=timeout,
        expect="platform runtime:",
        expect_optional=True,
    )
    run_cmd(session, "net-ip", timeout=timeout, expect="ipv4=10.0.2.15", expect_optional=True)
    run_cmd(session, "net-gw", timeout=timeout, expect="gateway=10.0.2.2")
    run_cmd(session, "net-dns", timeout=timeout, expect="dns=", expect_optional=True)
    run_cmd(
        session,
        "net-resolve example.com",
        timeout=timeout,
        expect="name=example.com ipv4=",
        expect_optional=True,
    )
    run_cmd(
        session,
        "hey gateway",
        timeout=timeout,
        expect="hello from (",
        expect_optional=True,
    )
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="theme=ocean")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="splash=enabled")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="language=en")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="update_channel=stable")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="network_mode=dhcp")
    run_cmd(session, "print-file /system/config.ini", timeout=timeout, expect="ipv4=10.0.2.42")
    run_cmd(
        session,
        "print-file /system/update/repository.ini",
        timeout=timeout,
        expect="branch=main",
    )
    run_cmd(
        session,
        "print-file /system/update/repository.ini",
        timeout=timeout,
        expect="manifest=/system/update/latest.ini",
    )
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
