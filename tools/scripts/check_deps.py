#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys

PREFERRED_TOOLCHAIN = [
    "x86_64-elf-gcc",
    "x86_64-elf-ld",
    "x86_64-elf-objcopy",
]

FALLBACK_TOOLCHAIN = [
    "x86_64-linux-gnu-gcc",
    "x86_64-linux-gnu-ld",
    "x86_64-linux-gnu-objcopy",
]

REQUIRED_RUNTIME_TOOLS = [
    "xorriso",
    "python3",
]

REQUIRED_RELEASE_SUFFIXES = [
    "include/efi/efi.h",
    "include/efi/x86_64/efibind.h",
    "lib/crt0-efi-x86_64.o",
    "lib/elf_x86_64_efi.lds",
]

def check_tool(tool):
    """Checks if a tool is available in the PATH."""
    return shutil.which(tool) is not None

def add_unique(items, value):
    if value and value not in items:
        items.append(value)

def brew_gnu_efi_prefix():
    brew = shutil.which("brew")
    if not brew:
        return None
    try:
        return subprocess.check_output(
            [brew, "--prefix", "gnu-efi"],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (subprocess.CalledProcessError, OSError):
        return None

def gnu_efi_prefixes():
    prefixes = []
    add_unique(prefixes, os.environ.get("EFI_PREFIX"))
    if sys.platform == "darwin":
        add_unique(prefixes, brew_gnu_efi_prefix())
        add_unique(prefixes, "/opt/homebrew/opt/gnu-efi")
        add_unique(prefixes, "/usr/local/opt/gnu-efi")
        add_unique(prefixes, "/opt/brew-master/opt/gnu-efi")
        add_unique(prefixes, "/usr")
    else:
        add_unique(prefixes, "/usr")
        add_unique(prefixes, brew_gnu_efi_prefix())
    return prefixes

def release_files_for(prefix):
    return [os.path.join(prefix, suffix) for suffix in REQUIRED_RELEASE_SUFFIXES]

def find_release_files():
    prefixes = gnu_efi_prefixes()
    if not prefixes:
        return "", [], REQUIRED_RELEASE_SUFFIXES
    for prefix in prefixes:
        files = release_files_for(prefix)
        missing = [path for path in files if not os.path.exists(path)]
        if not missing:
            return prefix, files, []
    files = release_files_for(prefixes[0])
    return prefixes[0], files, [path for path in files if not os.path.exists(path)]

def parse_args():
    parser = argparse.ArgumentParser(description="Check CapyOS build dependencies")
    parser.add_argument(
        "--allow-fallback-toolchain",
        action="store_true",
        help="accept x86_64-linux-gnu-* fallback when x86_64-elf-* is unavailable",
    )
    return parser.parse_args()

def main():
    args = parse_args()
    missing_runtime = []
    missing_release_files = []
    missing_preferred = []
    fallback_missing = []
    release_prefix, release_files, missing_release_files = find_release_files()
    print("Checking build dependencies...")

    if os.name == 'nt':
        print("[WARNING] Running on Windows. Use install-windows.ps1 to bootstrap WSL for 'make'.")

    print("Preferred release toolchain:")
    for tool in PREFERRED_TOOLCHAIN:
        if check_tool(tool):
            print(f"  [OK] {tool}")
        else:
            print(f"  [MISSING] {tool}")
            missing_preferred.append(tool)

    if missing_preferred:
        print("Fallback development toolchain:")
        for tool in FALLBACK_TOOLCHAIN:
            if check_tool(tool):
                print(f"  [OK] {tool}")
            else:
                print(f"  [MISSING] {tool}")
                fallback_missing.append(tool)

    print("Runtime/build helpers:")
    for tool in REQUIRED_RUNTIME_TOOLS:
        if check_tool(tool):
            print(f"  [OK] {tool}")
        else:
            print(f"  [MISSING] {tool}")
            missing_runtime.append(tool)

    print(f"UEFI release files (prefix: {release_prefix or 'not found'}):")
    for path in release_files:
        if os.path.exists(path):
            print(f"  [OK] {path}")
        else:
            print(f"  [MISSING] {path}")

    if missing_runtime or missing_release_files:
        print("\nERROR: Missing required runtime/build dependencies:")
        for t in missing_runtime:
            print(f" - {t}")
        for path in missing_release_files:
            print(f" - {path}")
        print("\nPlease install them with the OS-specific bootstrap script.")
        if missing_release_files:
            if sys.platform == "darwin":
                print("On macOS, run: bash install-macos.sh --skip-brew-update")
            elif os.name == "nt":
                print("On Windows, run: .\\install-windows.ps1")
            else:
                print("On Debian/Ubuntu, run: ./install-linux.sh")
        sys.exit(1)

    if missing_preferred:
        print("\nWARNING: Preferred bare-metal toolchain is incomplete.")
        if fallback_missing:
            print("No complete fallback GNU/Linux toolchain was detected either.")
            sys.exit(1)
        else:
            print("A fallback x86_64-linux-gnu toolchain is available for local development builds.")
        print("Release reproducibility remains blocked until x86_64-elf-* is installed.")
        if args.allow_fallback_toolchain:
            print("Fallback toolchain explicitly allowed by flag; continuing with success status.")
            sys.exit(0)
        sys.exit(1)

    print("\nAll preferred release dependencies found.")
    sys.exit(0)

if __name__ == "__main__":
    main()
