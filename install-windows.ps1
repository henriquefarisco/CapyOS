Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
& "$Root\tools\scripts\install_deps_windows.ps1" @args
