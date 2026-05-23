#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
case "$(uname -s)" in
  Darwin)
    exec bash "$ROOT/tools/scripts/install_deps_macos.sh" "$@"
    ;;
  Linux)
    exec bash "$ROOT/tools/scripts/install_deps.sh" "$@"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    echo "[err] No Windows, use PowerShell: .\\install-windows.ps1" >&2
    exit 1
    ;;
  *)
    echo "[err] Sistema nao suportado por install.sh: $(uname -s)" >&2
    exit 1
    ;;
esac
