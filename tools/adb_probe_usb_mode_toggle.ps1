param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [ValidateSet("full", "before", "after")]
    [string]$Mode = "full",

    [Parameter(Mandatory=$false)]
    [string]$BeforeSnapshot,

    [Parameter(Mandatory=$false)]
    [string]$OutDir = "work\settings-snapshots"
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Convert-ToSafeName([string]$Value) {
    $safe = $Value -replace "[^A-Za-z0-9._-]+", "_"
    $safe = $safe.Trim("_")
    if ([string]::IsNullOrWhiteSpace($safe)) {
        return "snapshot"
    }
    return $safe
}

function Get-LatestSnapshot([string]$Label) {
    $safeLabel = Convert-ToSafeName $Label
    $snapshotRoot = Join-Path $repoRoot $OutDir
    Get-ChildItem -LiteralPath $snapshotRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "*-$safeLabel" } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Invoke-SettingsSnapshot([string]$Label, [string]$CompareToPath = "") {
    $snapshotScript = Join-Path $PSScriptRoot "adb_snapshot_r1_settings.ps1"
    if (!(Test-Path -LiteralPath $snapshotScript)) {
        throw "Missing snapshot script: $snapshotScript"
    }

    if ($CompareToPath) {
        & $snapshotScript -Adb $adbPath -Label $Label -OutDir $OutDir -CompareTo $CompareToPath
    }
    else {
        & $snapshotScript -Adb $adbPath -Label $Label -OutDir $OutDir
    }
    if ($LASTEXITCODE -ne 0) {
        throw "settings snapshot failed"
    }

    $snapshot = Get-LatestSnapshot $Label
    if (!$snapshot) {
        throw "Could not locate snapshot for label $Label"
    }
    return $snapshot.FullName
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
$adbPath = Resolve-PathStrict $Adb

switch ($Mode) {
    "before" {
        $before = Invoke-SettingsSnapshot "before-usb-mode"
        Write-Host "Before snapshot: $before"
    }
    "after" {
        if (-not $BeforeSnapshot) {
            throw "-BeforeSnapshot is required when -Mode after is used."
        }
        $before = Resolve-PathStrict $BeforeSnapshot
        $after = Invoke-SettingsSnapshot "after-usb-mode" $before
        Write-Host "Before snapshot: $before"
        Write-Host "After snapshot : $after"
    }
    "full" {
        $before = Invoke-SettingsSnapshot "before-usb-mode"
        Write-Host ""
        Write-Host "Before snapshot: $before"
        Write-Host ""
        Write-Host "On the R1, change System -> USB device mode."
        Write-Host "For the ADB persistence investigation, the interesting change is Storage <-> Dock."
        Write-Host "After the device finishes changing mode, press Enter here."
        Read-Host | Out-Null
        $after = Invoke-SettingsSnapshot "after-usb-mode" $before
        Write-Host "Before snapshot: $before"
        Write-Host "After snapshot : $after"
    }
}
