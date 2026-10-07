"""Windows VMware gate adapter for the Linux/WSL recovery implementation."""
import json
from pathlib import Path
import re
import subprocess

from smoke_x64_vmware_installer import parse_flat_extent


def recover_vmware_copy(args, vmx, descriptor, repo_root):
    distribution = args.offline_recovery_wsl
    def linux(path):
        result = subprocess.run(["wsl", "-d", distribution, "--exec", "wslpath", "-u",
                                 str(Path(path).resolve())], capture_output=True,
                                text=True, check=True)
        return result.stdout.strip()
    output = descriptor.parent / "recovered-flat.vmdk"
    original = parse_flat_extent(descriptor)
    command = ["wsl", "-d", distribution, "--exec", "python3",
               linux(repo_root / "tools/scripts/offline_recovery.py"),
               "--source", linux(original), "--output", linux(output),
               "--vmx", linux(vmx), "--vmrun", linux(args.vmrun),
               "--bundle", linux(args.production_manifest.parent),
               "--helper", linux(repo_root / "build/offline-recovery-slot"),
               "--current", args.current_version]
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    evidence = json.loads(result.stdout)
    recovered = descriptor.parent / "recovered.vmdk"
    text = descriptor.read_text(encoding="utf-8")
    text, count = re.subn(r'^(RW \d+ FLAT ")[^"\r\n]+(" 0\s*)$',
                          rf'\g<1>{output.name}\g<2>', text, flags=re.MULTILINE)
    if count != 1:
        raise ValueError("unexpected disposable VMDK descriptor")
    with recovered.open("x", encoding="utf-8") as stream:
        stream.write(text)
    return recovered, original, evidence
