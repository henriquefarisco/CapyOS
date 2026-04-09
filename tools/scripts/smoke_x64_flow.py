#!/usr/bin/env python3
"""Compatibility facade for CapyOS x64 smoke flow helpers."""

from __future__ import annotations

from smoke_x64_auth import (
    assert_shell_identity,
    cancel_iso_install,
    complete_iso_install,
    login,
    maybe_run_first_boot_setup,
)
from smoke_x64_boot import smoke_first_boot, smoke_second_boot
from smoke_x64_helpers import (
    run_cmd,
    run_cmd_expect_prompt,
    run_open_write,
    trigger_poweroff,
    trigger_reboot,
    wait_and_send,
    wait_for_vm_exit,
)

__all__ = [
    "assert_shell_identity",
    "cancel_iso_install",
    "complete_iso_install",
    "login",
    "maybe_run_first_boot_setup",
    "run_cmd",
    "run_cmd_expect_prompt",
    "run_open_write",
    "smoke_first_boot",
    "smoke_second_boot",
    "trigger_poweroff",
    "trigger_reboot",
    "wait_and_send",
    "wait_for_vm_exit",
]
