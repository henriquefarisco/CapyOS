#!/usr/bin/env python3
from __future__ import annotations

import struct

SECTOR = 512

BOOT_CONFIG_MAGIC = 0xB001CF61
BOOT_CONFIG_VERSION = 3
BOOT_CONFIG_FLAG_HAS_VOLUME_KEY = 0x0001
BOOT_CONFIG_LAYOUT_LEN = 16
BOOT_CONFIG_LANGUAGE_LEN = 16
BOOT_CONFIG_KEY_LEN = 64
BOOT_CONFIG_RESERVED_LEN = 408


def normalize_keyboard_layout(raw: str | None) -> str:
    layout = (raw or "us").strip().lower()
    if not layout:
        layout = "us"
    if len(layout) >= BOOT_CONFIG_LAYOUT_LEN:
        raise SystemExit(
            f"[err] keyboard layout too long: {layout!r} "
            f"(max {BOOT_CONFIG_LAYOUT_LEN - 1})"
        )
    for ch in layout:
        if not (ch.isalnum() or ch in "-_"):
            raise SystemExit(f"[err] invalid keyboard layout: {layout!r}")
    return layout


def normalize_language(raw: str | None) -> str:
    language = (raw or "en").strip()
    if not language:
        language = "en"
    lowered = language.lower().replace("_", "-")
    if lowered == "pt":
        lowered = "pt-br"
    if lowered in ("en", "en-us"):
        language = "en"
    elif lowered in ("pt-br", "pt"):
        language = "pt-BR"
    elif lowered in ("es", "es-es"):
        language = "es"
    else:
        raise SystemExit(f"[err] unsupported language: {language!r}")
    if len(language) >= BOOT_CONFIG_LANGUAGE_LEN:
        raise SystemExit(
            f"[err] language too long: {language!r} "
            f"(max {BOOT_CONFIG_LANGUAGE_LEN - 1})"
        )
    return language


def normalize_volume_key(raw: str | None) -> str:
    if raw is None:
        return ""

    out: list[str] = []
    for ch in raw:
        if ch in "- \t\r\n":
            continue
        if not ch.isalnum():
            raise SystemExit(
                "[err] volume key must contain only letters/numbers "
                "(hyphens optional)."
            )
        out.append(ch.upper())

    key = "".join(out)
    if not key:
        return ""
    if len(key) < 8:
        raise SystemExit("[err] volume key too short after normalization (min 8).")
    if len(key) >= BOOT_CONFIG_KEY_LEN:
        raise SystemExit(
            f"[err] volume key too long (max {BOOT_CONFIG_KEY_LEN - 1} chars)."
        )
    return key


def build_boot_config(
    layout: str | None, language: str | None, volume_key: str | None
) -> bytes:
    layout_norm = normalize_keyboard_layout(layout)
    language_norm = normalize_language(language)
    key_norm = normalize_volume_key(volume_key)
    flags = BOOT_CONFIG_FLAG_HAS_VOLUME_KEY if key_norm else 0

    payload = struct.pack(
        "<IHH16s16s64s408s",
        BOOT_CONFIG_MAGIC,
        BOOT_CONFIG_VERSION,
        flags,
        layout_norm.encode("ascii").ljust(BOOT_CONFIG_LAYOUT_LEN, b"\x00"),
        language_norm.encode("ascii").ljust(BOOT_CONFIG_LANGUAGE_LEN, b"\x00"),
        key_norm.encode("ascii").ljust(BOOT_CONFIG_KEY_LEN, b"\x00"),
        b"\x00" * BOOT_CONFIG_RESERVED_LEN,
    )
    if len(payload) != SECTOR:
        raise SystemExit(
            f"[err] CAPYCFG.BIN invalid size: {len(payload)} (expected {SECTOR})"
        )
    return payload
