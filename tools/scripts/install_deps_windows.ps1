param(
    [switch]$SkipWslInstall,
    [switch]$SkipWslBootstrap,
    [switch]$SkipPackages,
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
    $nativeExitCode = $LASTEXITCODE
    if ($nativeExitCode -ne 0) {
        throw "Comando falhou com codigo ${nativeExitCode}: $Command"
    }
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

function Resolve-WindowsTool {
    param([string]$Name, [string[]]$Candidates)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }
    return $null
}

function Assert-WindowsTools {
    if ($SkipSmoke) {
        Write-Info "Pulando preflight Python/VMware por solicitacao"
        return
    }
    if (-not (Test-CommandExists "py.exe")) {
        throw "Python Launcher (py.exe) nao encontrado; instale Python 3 para os gates VMware."
    }
    Invoke-Step "py.exe -3 --version" { py.exe -3 --version }

    $programFilesX86 = ${env:ProgramFiles(x86)}
    $vmwareRoots = @(
        (Join-Path $env:ProgramFiles "VMware\VMware Workstation"),
        $(if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
            Join-Path $programFilesX86 "VMware\VMware Workstation"
        })
    )
    $vmrunCandidates = @($vmwareRoots | ForEach-Object {
        if ($_ ) { Join-Path $_ "vmrun.exe" }
    })
    $vdiskCandidates = @($vmwareRoots | ForEach-Object {
        if ($_ ) { Join-Path $_ "vmware-vdiskmanager.exe" }
    })
    if ($null -eq (Resolve-WindowsTool "vmrun.exe" $vmrunCandidates)) {
        throw "vmrun.exe nao encontrado; instale VMware Workstation para o gate oficial."
    }
    if ($null -eq (Resolve-WindowsTool "vmware-vdiskmanager.exe" $vdiskCandidates)) {
        throw "vmware-vdiskmanager.exe nao encontrado; instale VMware Workstation para o gate oficial."
    }
}

function Get-WslProjectPath {
    param([string]$NativePath)
    if ($ProjectPath.Length -gt 0) {
        return $ProjectPath
    }
    $fullPath = [System.IO.Path]::GetFullPath($NativePath)
    if ($DryRun) {
        Write-Warn "Dry-run sem consultar wslpath; informe -ProjectPath para validar um mapeamento especifico."
        return "<caminho-resolvido-por-wslpath>"
    }
    # Windows PowerShell 5.1 otherwise lets wsl.exe reinterpret backslashes in
    # an unquoted native argument (for example, C:\repo becomes C:repo).
    $wslpathInput = [char]34 + $fullPath + [char]34
    $resolved = (& wsl.exe -d $Distro -- wslpath -a -u $wslpathInput) -replace "`0", ""
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($resolved)) {
        throw "wslpath nao conseguiu resolver o checkout Windows para a distro '$Distro': $fullPath"
    }
    return $resolved.Trim()
}

function Ensure-Wsl {
    if (-not (Test-CommandExists "wsl.exe")) {
        throw "wsl.exe nao encontrado; instale/ative o WSL no Windows e reinicie."
    }

    $statusOutput = @(& wsl.exe --status 2>&1)
    $statusExitCode = $LASTEXITCODE
    if ($statusExitCode -eq 0) {
        return
    }
    if ($SkipWslInstall) {
        throw "WSL existe, mas nao esta pronto (codigo $statusExitCode): $($statusOutput -join ' ')"
    }
    Write-Info "Ativando os componentes do WSL"
    Invoke-Step "wsl --install --no-distribution" {
        wsl.exe --install --no-distribution
    }
    throw "Reinicie o Windows para ativar o WSL e execute este script novamente."
}

function Ensure-Distro {
    $installedOutput = @(& wsl.exe -l -q 2>&1)
    $listExitCode = $LASTEXITCODE
    if ($listExitCode -ne 0) {
        throw "Nao foi possivel listar distros WSL (codigo ${listExitCode}): $($installedOutput -join ' ')"
    }
    $installed = @($installedOutput | ForEach-Object {
        ($_ -replace "`0", "").Trim()
    } | Where-Object { $_.Length -gt 0 })
    if ($installed -contains $Distro) {
        return
    }
    if ($SkipWslInstall) {
        throw "Distro WSL '$Distro' nao encontrada e -SkipWslInstall foi informado."
    }
    Write-Info "Instalando distro WSL $Distro"
    Invoke-Step "wsl --install -d $Distro --no-launch" {
        wsl.exe --install -d $Distro --no-launch
    }
    throw "Abra a distro $Distro uma vez para concluir o primeiro usuario e execute este script novamente."
}

function Assert-DistroReady {
    $uidOutput = @(& wsl.exe -d $Distro -- sh -lc "id -u" 2>&1)
    $uidExitCode = $LASTEXITCODE
    if ($uidExitCode -ne 0) {
        throw "A distro '$Distro' nao iniciou (codigo ${uidExitCode}). Abra-a uma vez, crie o usuario e tente novamente: $($uidOutput -join ' ')"
    }
    $uid = (($uidOutput -join "") -replace "`0", "").Trim()
    if ($uid -eq "0") {
        throw "A distro '$Distro' usa root como usuario padrao. Configure um usuario normal antes do bootstrap."
    }
}

function Bootstrap-Inside-Wsl {
    if ($SkipWslBootstrap) {
        Write-Info "Pulando bootstrap dentro do WSL por solicitacao"
        return
    }
    $wslPath = Get-WslProjectPath $ProjectRoot
    $installArgs = @()
    if ($SkipPackages) {
        $installArgs += "--skip-packages"
    }
    if ($SkipSmoke) {
        $installArgs += "--skip-smoke"
    }
    # Keep the checkout path out of a nested shell command.  Explicit quote
    # characters are required for Windows PowerShell 5.1 -> wsl.exe interop.
    $wslCdInput = [char]34 + $wslPath + [char]34
    Write-Info "Executando bootstrap Linux dentro do WSL em $wslPath"
    Invoke-Step "wsl -d $Distro --cd <checkout> -- bash ./install-linux.sh" {
        wsl.exe -d $Distro --cd $wslCdInput -- bash ./install-linux.sh @installArgs
    }
}

function Print-Summary {
    $wslPath = Get-WslProjectPath $ProjectRoot
    Write-Host ""
    Write-Host "[ok] Ambiente Windows/WSL preparado."
    Write-Host ""
    Write-Host "Comandos sugeridos:"
    Write-Host "  wsl -d $Distro --cd `"$wslPath`" -- bash -lc `"source ~/.profile && make test`""
    Write-Host "  wsl -d $Distro --cd `"$wslPath`" -- bash -lc `"source ~/.profile && make all64 TOOLCHAIN64=elf`""
    Write-Host "  wsl -d $Distro --cd `"$wslPath`" -- bash -lc `"source ~/.profile && make iso-uefi TOOLCHAIN64=elf`""
    Write-Host ""
}

Assert-Windows
Assert-WindowsTools
Ensure-Wsl
Ensure-Distro
Assert-DistroReady
Bootstrap-Inside-Wsl
Print-Summary
