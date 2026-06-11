param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [ValidateSet("status", "enable", "disable")]
    [string]$Action = "status"
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Invoke-AdbText([string]$Command) {
    $output = & $adbPath shell $Command
    if ($LASTEXITCODE -ne 0) {
        throw "adb shell failed: $Command"
    }
    return ($output -join "`n")
}

function Read-BootAdbStatus {
    $statusCommand = @'
echo "s90adb=$(if [ -x /etc/init.d/S90adb ]; then echo yes; elif [ -e /etc/init.d/S90adb ]; then echo present-not-exec; else echo no; fi)"
echo "t90adb=$(if [ -x /etc/init.d/T90adb ]; then echo yes; elif [ -e /etc/init.d/T90adb ]; then echo present-not-exec; else echo no; fi)"
echo "disableadb=$(if [ -e /usr/data/disableadb ]; then echo yes; else echo no; fi)"
echo "adbd=$(ps | grep '[a]dbd' >/dev/null && echo running || echo stopped)"
echo "adb_gadget=$(if [ -d /sys/kernel/config/usb_gadget/adb_demo ]; then echo yes; else echo no; fi)"
echo "version=$(sed -n 's/^version=//p' /etc/r1_audiobook_version 2>/dev/null | head -1)"
echo "boot_adb_marker=$(sed -n 's/^boot_adb=//p' /etc/r1_audiobook_version 2>/dev/null | head -1)"
'@
    $text = Invoke-AdbText $statusCommand
    $map = @{}
    foreach ($line in ($text -split "`n")) {
        if ($line -match "^([^=]+)=(.*)$") {
            $map[$Matches[1]] = $Matches[2]
        }
    }
    return [pscustomobject]@{
        Raw = $text
        Values = $map
    }
}

function Write-BootAdbAdvice($Status) {
    $values = $Status.Values
    Write-Host $Status.Raw
    Write-Host ""
    if ($values["s90adb"] -eq "yes" -and $values["disableadb"] -eq "no") {
        Write-Host "OK   ADB should be allowed to start on the next boot."
    }
    elseif ($values["s90adb"] -eq "yes" -and $values["disableadb"] -eq "yes") {
        Write-Host "INFO Firmware has /etc/init.d/S90adb, but /usr/data/disableadb blocks boot ADB."
        Write-Host "     Run with -Action enable to remove the marker for the next reboot."
    }
    elseif ($values["t90adb"] -eq "yes") {
        Write-Host "INFO Stock ADB helper exists as /etc/init.d/T90adb, but boot persistence needs /etc/init.d/S90adb."
        Write-Host "     Build a development firmware with tools\build_r1_audiobook_firmware.ps1 -EnableBootAdb."
    }
    else {
        Write-Host "WARN No usable stock ADB init helper was found."
    }

    if ($values["adbd"] -eq "running") {
        Write-Host "OK   adbd is running in the current session."
    }
    else {
        Write-Host "INFO adbd is not currently running."
    }
}

$adbPath = Resolve-PathStrict $Adb

& $adbPath devices | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

switch ($Action) {
    "enable" {
        Invoke-AdbText "rm -f /usr/data/disableadb; sync" | Out-Null
        Write-Host "Removed /usr/data/disableadb. This affects the next boot if /etc/init.d/S90adb exists."
    }
    "disable" {
        Invoke-AdbText "touch /usr/data/disableadb; sync" | Out-Null
        Write-Host "Created /usr/data/disableadb. This should block boot ADB on the next reboot."
        Write-Host "The current ADB session is left running."
    }
}

$status = Read-BootAdbStatus
Write-BootAdbAdvice $status
