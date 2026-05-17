#!/usr/bin/env python3
from __future__ import annotations

import sys

FAILURE_MARKERS = (
    "kernel panic",
    "panic:",
    "triple fault",
    "general protection fault",
)


def markers_in_order(text: str, markers: tuple[str, ...]) -> bool:
    low = text.lower()
    offset = 0
    for marker in markers:
        pos = low.find(marker.lower(), offset)
        if pos < 0:
            return False
        offset = pos + len(marker)
    return True


def first_failure_marker(text: str) -> str:
    low = text.lower()
    for marker in FAILURE_MARKERS:
        if marker in low:
            return marker
    return ""


def unique_markers(markers: tuple[str, ...]) -> tuple[str, ...]:
    return tuple(dict.fromkeys(markers))


def self_test() -> tuple[bool, str]:
    markers = ("alpha", "beta", "gamma")
    if not markers_in_order("ALPHA\nnoise\nbeta\ngamma", markers):
        return False, "markers em ordem foram rejeitados"
    if markers_in_order("beta\nalpha\ngamma", markers):
        return False, "markers fora de ordem foram aceitos"
    if markers_in_order("alpha\ngamma", markers):
        return False, "marker ausente foi aceito"
    if not markers_in_order("ready\nnoise\nready", ("ready", "ready")):
        return False, "markers repetidos em ordem foram rejeitados"
    if markers_in_order("ready", ("ready", "ready")):
        return False, "marker repetido sem segunda ocorrencia foi aceito"
    if unique_markers(("alpha", "beta", "alpha", "gamma")) != markers:
        return False, "markers duplicados nao foram deduplicados em ordem"
    if first_failure_marker("alpha\npanic:\nbeta\ngamma") != "panic:":
        return False, "marker de falha nao foi detectado"
    if first_failure_marker("alpha\nbeta\ngamma") != "":
        return False, "marker de falha inexistente foi detectado"
    return True, ""


def run_self_test() -> int:
    ok, message = self_test()
    if not ok:
        print(f"[err] self-test smoke_marker_policy: {message}", file=sys.stderr)
        return 1
    print("[ok] self-test smoke_marker_policy passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(run_self_test())
