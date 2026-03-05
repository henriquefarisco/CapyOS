#!/usr/bin/env python3
import shutil
import sys
import os

REQUIRED_TOOLS = [
    # Toolchain x86_64 bare-metal
    "x86_64-elf-gcc",
    "x86_64-elf-ld",
    "x86_64-elf-objcopy",
    # ISO/EFI tooling
    "xorriso",
    # Runtime tooling for provisioning/smoke scripts
    "python3",
]

def check_tool(tool):
    """Checks if a tool is available in the PATH."""
    return shutil.which(tool) is not None

def main():
    missing = []
    print("Checking build dependencies...")
    
    if os.name == 'nt':
        print("[WARNING] Running on Windows. Ensure you are using WSL for 'make'.")
        
    for tool in REQUIRED_TOOLS:
        if check_tool(tool):
            print(f"  [OK] {tool}")
        else:
            print(f"  [MISSING] {tool}")
            missing.append(tool)
            
    if missing:
        print("\nERROR: Missing required tools:")
        for t in missing:
            print(f" - {t}")
        print("\nPlease install them in your WSL environment.")
        sys.exit(1)
        
    print("\nAll dependencies found.")
    sys.exit(0)

if __name__ == "__main__":
    main()
