#!/usr/bin/env python3
import argparse
import shutil
import sys
import os

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

def check_tool(tool):
    """Checks if a tool is available in the PATH."""
    return shutil.which(tool) is not None

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
    missing_preferred = []
    fallback_missing = []
    print("Checking build dependencies...")
    
    if os.name == 'nt':
        print("[WARNING] Running on Windows. Ensure you are using WSL for 'make'.")
        
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

    if missing_runtime:
        print("\nERROR: Missing required runtime/build helpers:")
        for t in missing_runtime:
            print(f" - {t}")
        print("\nPlease install them in your WSL environment.")
        sys.exit(1)

    if missing_preferred:
        print("\nWARNING: Preferred bare-metal toolchain is incomplete.")
        if fallback_missing:
            print("No complete fallback GNU/Linux toolchain was detected either.")
            sys.exit(1)

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
