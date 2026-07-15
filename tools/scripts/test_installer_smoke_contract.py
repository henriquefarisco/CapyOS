#!/usr/bin/env python3

from smoke_x64_auth import (
    installer_has_single_eligible_target,
    require_first_boot_wizard,
)


def main() -> int:
    cases = {
        "[installer] eligible-targets=1\n": True,
        "prefix\n[installer] eligible-targets=1\r\nnext\n": True,
        "[installer] eligible-targets=0\n": False,
        "[installer] eligible-targets=2\n": False,
        "[installer] eligible-targets=10\n": False,
        "[installer] eligible-targets=16\n": False,
        "noise eligible-targets=1\n": False,
    }
    for text, expected in cases.items():
        actual = installer_has_single_eligible_target(text)
        if actual != expected:
            print(
                "[FAIL] installer smoke contract:",
                repr(text),
                "got",
                actual,
                "expected",
                expected,
            )
            return 1
    require_first_boot_wizard("Available keyboard layouts:", True)
    require_first_boot_wizard("User:", False)
    for marker in ("User:", "Usuario:", "Provisionamento automatico"):
        try:
            require_first_boot_wizard(marker, True)
        except RuntimeError:
            continue
        print("[FAIL] fresh-install smoke accepted direct login:", marker)
        return 1
    print("[OK] installer smoke contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
