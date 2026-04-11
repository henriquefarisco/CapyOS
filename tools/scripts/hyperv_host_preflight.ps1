param(
    [string]$Name = "",
    [string]$OutDir = "build/hyperv-preflight",
    [switch]$IncludeEvents
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Info {
    param([string]$Message)
    Write-Host "[info] $Message"
}

function Write-ErrorLine {
    param([string]$Message)
    Write-Host "[error] $Message"
}

function Test-IsElevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-OptionalProperty {
    param(
        [object]$InputObject,
        [string]$Name
    )

    if ($null -eq $InputObject) {
        return $null
    }

    $prop = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $prop) {
        return $null
    }

    return $prop.Value
}

function Export-JsonFile {
    param(
        [string]$Path,
        [object]$Value
    )

    $Value | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -Path $Path
}

if (-not (Test-IsElevated)) {
    Write-ErrorLine "Hyper-V preflight requires an elevated PowerShell session."
    Write-Info "Open PowerShell as Administrator and run this script again."
    exit 2
}

if (-not (Get-Command Get-VM -ErrorAction SilentlyContinue)) {
    Write-ErrorLine "Hyper-V PowerShell cmdlets are not available in this session."
    exit 3
}

$resolvedOutDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir)
New-Item -ItemType Directory -Force -Path $resolvedOutDir | Out-Null

$report = [ordered]@{
    timestamp_utc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    computer_name = $env:COMPUTERNAME
    elevated = $true
    vm_name = $Name
    hyperv_module = (Get-Command Get-VM).Source
}

$vmList = @()
$vms = Get-VM | Sort-Object Name
foreach ($vm in $vms) {
    $vmList += [ordered]@{
        name = $vm.Name
        state = [string]$vm.State
        generation = $vm.Generation
        version = $vm.Version
        uptime = [string]$vm.Uptime
        status = [string]$vm.Status
        id = [string]$vm.Id
    }
}
$report.vms = $vmList

if ($Name -ne "") {
    $vm = Get-VM -Name $Name -ErrorAction Stop
    $firmware = Get-VMFirmware -VMName $Name
    $memory = Get-VMMemory -VMName $Name
    $netAdapters = @(Get-VMNetworkAdapter -VMName $Name)
    $hardDisks = @(Get-VMHardDiskDrive -VMName $Name)
    $dvdDrives = @(Get-VMDvdDrive -VMName $Name)
    $comPorts = @(Get-VMComPort -VMName $Name)

    $report.vm = [ordered]@{
        name = $vm.Name
        id = [string]$vm.Id
        state = [string]$vm.State
        generation = $vm.Generation
        version = $vm.Version
        automatic_start_action = [string]$vm.AutomaticStartAction
        automatic_stop_action = [string]$vm.AutomaticStopAction
        secure_boot = Get-OptionalProperty $firmware "SecureBoot"
        secure_boot_template = Get-OptionalProperty $firmware "SecureBootTemplate"
        preferred_network_boot_protocol = [string](Get-OptionalProperty $firmware "PreferredNetworkBootProtocol")
        dynamic_memory_enabled = Get-OptionalProperty $memory "DynamicMemoryEnabled"
        startup_memory = Get-OptionalProperty $memory "Startup"
        minimum_memory = Get-OptionalProperty $memory "Minimum"
        maximum_memory = Get-OptionalProperty $memory "Maximum"
        processor_count = (Get-VMProcessor -VMName $Name).Count
        network_adapters = @(
            foreach ($adapter in $netAdapters) {
                [ordered]@{
                    name = $adapter.Name
                    switch_name = $adapter.SwitchName
                    status = [string]$adapter.Status
                    mac_address = $adapter.MacAddress
                    is_legacy = Get-OptionalProperty $adapter "IsLegacy"
                    device_naming = Get-OptionalProperty $adapter "DeviceNaming"
                }
            }
        )
        hard_disks = @(
            foreach ($disk in $hardDisks) {
                [ordered]@{
                    controller_type = [string]$disk.ControllerType
                    controller_number = $disk.ControllerNumber
                    controller_location = $disk.ControllerLocation
                    path = $disk.Path
                }
            }
        )
        dvd_drives = @(
            foreach ($dvd in $dvdDrives) {
                [ordered]@{
                    controller_number = $dvd.ControllerNumber
                    controller_location = $dvd.ControllerLocation
                    path = $dvd.Path
                }
            }
        )
        com_ports = @(
            foreach ($port in $comPorts) {
                [ordered]@{
                    number = Get-OptionalProperty $port "Number"
                    path = Get-OptionalProperty $port "Path"
                    disconnected = Get-OptionalProperty $port "Disconnected"
                }
            }
        )
    }

    if ($IncludeEvents) {
        $workerEvents = @(Get-WinEvent -LogName "Microsoft-Windows-Hyper-V-Worker/Admin" -MaxEvents 200 |
            Where-Object { $_.Message -like "*$Name*" } |
            Select-Object -First 40 TimeCreated, Id, LevelDisplayName, Message)
        $vmmsEvents = @(Get-WinEvent -LogName "Microsoft-Windows-Hyper-V-VMMS/Admin" -MaxEvents 200 |
            Where-Object { $_.Message -like "*$Name*" } |
            Select-Object -First 40 TimeCreated, Id, LevelDisplayName, Message)

        $report.events = [ordered]@{
            worker_admin = $workerEvents
            vmms_admin = $vmmsEvents
        }
    }
}

$jsonPath = Join-Path $resolvedOutDir "hyperv-preflight.json"
$txtPath = Join-Path $resolvedOutDir "hyperv-preflight.txt"

Export-JsonFile -Path $jsonPath -Value $report

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("Hyper-V preflight")
$lines.Add("timestamp_utc=$($report.timestamp_utc)")
$lines.Add("computer_name=$($report.computer_name)")
$lines.Add("elevated=$($report.elevated)")
$lines.Add("hyperv_module=$($report.hyperv_module)")
$lines.Add("")
$lines.Add("vm_count=$($vmList.Count)")
foreach ($item in $vmList) {
    $lines.Add("vm name=$($item.name) state=$($item.state) gen=$($item.generation) version=$($item.version) status=$($item.status)")
}

if ($report.Contains("vm")) {
    $vmData = $report.vm
    $lines.Add("")
    $lines.Add("selected_vm=$($vmData.name)")
    $lines.Add("generation=$($vmData.generation)")
    $lines.Add("state=$($vmData.state)")
    $lines.Add("secure_boot=$($vmData.secure_boot)")
    $lines.Add("secure_boot_template=$($vmData.secure_boot_template)")
    $lines.Add("preferred_network_boot_protocol=$($vmData.preferred_network_boot_protocol)")
    $lines.Add("dynamic_memory_enabled=$($vmData.dynamic_memory_enabled)")
    $lines.Add("startup_memory=$($vmData.startup_memory)")
    $lines.Add("minimum_memory=$($vmData.minimum_memory)")
    $lines.Add("maximum_memory=$($vmData.maximum_memory)")
    $lines.Add("processor_count=$($vmData.processor_count)")
    foreach ($adapter in $vmData.network_adapters) {
        $lines.Add("net name=$($adapter.name) switch=$($adapter.switch_name) status=$($adapter.status) mac=$($adapter.mac_address) legacy=$($adapter.is_legacy)")
    }
    foreach ($disk in $vmData.hard_disks) {
        $lines.Add("disk type=$($disk.controller_type) number=$($disk.controller_number) location=$($disk.controller_location) path=$($disk.path)")
    }
    foreach ($dvd in $vmData.dvd_drives) {
        $lines.Add("dvd number=$($dvd.controller_number) location=$($dvd.controller_location) path=$($dvd.path)")
    }
    foreach ($port in $vmData.com_ports) {
        $lines.Add("com number=$($port.number) path=$($port.path) disconnected=$($port.disconnected)")
    }
}

$lines | Set-Content -Encoding UTF8 -Path $txtPath

Write-Info "Preflight exported:"
Write-Info "  $jsonPath"
Write-Info "  $txtPath"
if ($Name -ne "") {
    Write-Info "Use this together with guest logs: runtime-native show, net-status, net-dump-runtime, and the full serial log."
}
