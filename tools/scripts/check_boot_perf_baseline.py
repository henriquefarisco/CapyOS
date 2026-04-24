#!/usr/bin/env python3
"""Check CapyOS boot performance smoke logs against a JSON baseline."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

BOOT_TOTAL_RE = re.compile(r"total_boot_to_login:\s*(\d+)\s*us")


def parse_total_boot_us(text: str) -> int:
    matches = BOOT_TOTAL_RE.findall(text)
    if not matches:
        raise ValueError("metric not found: total_boot_to_login")
    return int(matches[-1], 10)


def load_baseline(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as fp:
        data = json.load(fp)
    total = int(data.get("total_boot_to_login_us", 0))
    if total <= 0:
        raise ValueError(f"invalid baseline metric in {path}")
    return data


def write_baseline(path: Path, total_us: int, source_log: Path, max_regression_percent: float) -> None:
    if total_us <= 0:
        raise ValueError("refusing to write a non-positive boot baseline")
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {
        "metric": "total_boot_to_login",
        "total_boot_to_login_us": total_us,
        "max_regression_percent": max_regression_percent,
        "source_log": str(source_log),
    }
    with path.open("w", encoding="utf-8") as fp:
        json.dump(data, fp, indent=2, sort_keys=True)
        fp.write("\n")


def check_regression(total_us: int, baseline: dict[str, Any], fallback_percent: float) -> tuple[bool, int, float]:
    base_us = int(baseline["total_boot_to_login_us"])
    percent = float(baseline.get("max_regression_percent", fallback_percent))
    limit_us = int(base_us * (1.0 + percent / 100.0))
    return total_us <= limit_us, limit_us, percent


def run_self_test() -> int:
    sample = """
Boot performance:
  stages: 2
  platform: 100 us
  total_boot_to_login: 2500000 us
"""
    if parse_total_boot_us(sample) != 2500000:
        print("[err] parser self-test failed", file=sys.stderr)
        return 1
    ok, limit, percent = check_regression(
        1100,
        {"total_boot_to_login_us": 1000, "max_regression_percent": 10.0},
        20.0,
    )
    if not ok or limit != 1100 or percent != 10.0:
        print("[err] regression self-test failed", file=sys.stderr)
        return 1
    ok, _, _ = check_regression(
        1101,
        {"total_boot_to_login_us": 1000, "max_regression_percent": 10.0},
        20.0,
    )
    if ok:
        print("[err] regression failure self-test failed", file=sys.stderr)
        return 1
    print("[ok] boot performance baseline self-test passed")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", help="Smoke log containing perf-boot output")
    parser.add_argument(
        "--baseline",
        default="docs/performance/boot-baseline.json",
        help="JSON baseline path",
    )
    parser.add_argument(
        "--max-regression-percent",
        type=float,
        default=20.0,
        help="Allowed regression when baseline does not override it",
    )
    parser.add_argument(
        "--write-baseline",
        action="store_true",
        help="Create or replace the baseline from the provided log",
    )
    parser.add_argument("--self-test", action="store_true", help="Run parser/regression self-test")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return run_self_test()

    if not args.log:
        print("[err] --log is required unless --self-test is used", file=sys.stderr)
        return 2

    log_path = Path(args.log)
    baseline_path = Path(args.baseline)
    total_us = parse_total_boot_us(log_path.read_text(encoding="latin-1", errors="replace"))

    if args.write_baseline:
        write_baseline(
            baseline_path,
            total_us,
            log_path,
            args.max_regression_percent,
        )
        print(f"[ok] boot baseline written: {baseline_path} total_boot_to_login={total_us} us")
        return 0

    baseline = load_baseline(baseline_path)
    ok, limit_us, percent = check_regression(total_us, baseline, args.max_regression_percent)
    if not ok:
        base_us = int(baseline["total_boot_to_login_us"])
        print(
            "[err] boot regression: "
            f"current={total_us} us baseline={base_us} us "
            f"limit={limit_us} us allowed={percent:.2f}%",
            file=sys.stderr,
        )
        return 1

    print(f"[ok] boot performance within baseline: current={total_us} us limit={limit_us} us")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
