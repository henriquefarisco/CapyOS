param(
    [switch]$SkipWslInstall,
    [switch]$SkipWslBootstrap,
    [switch]$SkipSmoke,
    [switch]$DryRun,
    [string]$Distro = "Ubuntu",
    [string]$ProjectPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $ScriptRoot)

function Write-Info {
    param([string]$Message)
    Write-Host "[info] $Message"
}

function Write-Warn {
    param([string]$Message)
    Write-Warning $Message
}

function Invoke-Step {
    param([string]$Command, [scriptblock]$Action)
    if ($DryRun) {
        Write-Host "[dry-run] $Command"
        return
    }
    & $Action
}

function Assert-Windows {
    if ($env:OS -ne "Windows_NT") {
        throw "Este instalador e exclusivo para Windows. Use ./install.sh no Linux/WSL."
    }
}

function Test-CommandExists {
    param([string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Get-WslProjectPath {
    param([string]$NativePath)
    if ($ProjectPath.Length -gt 0) {
        return $ProjectPath
    }
    $fullPath = [System.IO.Path]::GetFullPath($NativePath)
    if ($fullPath -match '^([A-Za-z]):\\(.*)$') {
        $drive = $Matches[1].ToLowerInvariant()
        $rest = $Matches[2] -replace '\\','/'
        return "/mnt/$drive/$rest"
    }
    throw "Nao foi possivel converter caminho Windows para WSL: $fullPath"
}

function Ensure-Wsl {
    if (Test-CommandExists "wsl.exe") {
        return
    }
    if ($SkipWslInstall) {
        throw "wsl.exe nao encontrado e -SkipWslInstall foi informado."
    }
    Write-Info "Instalando WSL com distro $Distro"
    Invoke-Step "wsl --install -d $Distro" { wsl --install -d $Distro }
    throw "Conclua a instalacao/reinicializacao do WSL e execute este script novamente."
}

function Ensure-Distro {
    $installed = (& wsl -l -q) -replace "`0", ""
    if ($installed -contains $Distro) {
        return
    }
    if ($SkipWslInstall) {
        throw "Distro WSL '$Distro' nao encontrada e -SkipWslInstall foi informado."
    }
    Write-Info "Instalando distro WSL $Distro"
    Invoke-Step "wsl --install -d $Distro" { wsl --install -d $Distro }
    throw "Abra a distro $Distro uma vez para concluir o primeiro usuario e execute este script novamente."
}

function Bootstrap-Inside-Wsl {
    if ($SkipWslBootstrap) {
        Write-Info "Pulando bootstrap dentro do WSL por solicitacao"
        return
    }
    $wslPath = Get-WslProjectPath $ProjectRoot
    $installArgs = ""
    if ($SkipSmoke) {
        $installArgs = " --skip-smoke"
    }
    $command = "cd '$wslPath' && bash ./install-linux.sh$installArgs"
    Write-Info "Executando bootstrap Linux dentro do WSL em $wslPath"
    Invoke-Step "wsl -d $Distro -- bash -lc $command" { wsl -d $Distro -- bash -lc $command }
}

function Print-Summary {
    $wslPath = Get-WslProjectPath $ProjectRoot
    Write-Host ""
    Write-Host "[ok] Ambiente Windows/WSL preparado."
    Write-Host ""
    Write-Host "Comandos sugeridos:"
    Write-Host "  wsl -d $Distro -- bash -lc \"cd '$wslPath' && source ~/.bashrc && make test\""
    Write-Host "  wsl -d $Distro -- bash -lc \"cd '$wslPath' && source ~/.bashrc && make all64 TOOLCHAIN64=elf\""
    Write-Host "  wsl -d $Distro -- bash -lc \"cd '$wslPath' && source ~/.bashrc && make iso-uefi TOOLCHAIN64=elf\""
    Write-Host ""
}

Assert-Windows
Ensure-Wsl
Ensure-Distro
Bootstrap-Inside-Wsl
Print-Summary
