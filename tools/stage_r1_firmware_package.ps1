param(
    [Parameter(Mandatory=$false)]
    [string]$Package = "work\audiobook-firmware-1.6.28-sd-ready-dev\r1-audiobooks-1.6.28-sd-ready-dev.upt",

    [Parameter(Mandatory=$false)]
    [string]$BuildOutDir = "",

    [Parameter(Mandatory=$false)]
    [string]$ExpectedVersion = "1.6.28-sd-ready-dev",

    [Parameter(Mandatory=$false)]
    [string]$ExpectedLabel = "HiBy R1 Audiobook FW 1.6.28",

    [Parameter(Mandatory=$false)]
    [string]$SdRoot = "",

    [Parameter(Mandatory=$false)]
    [string]$Adb = "",

    [switch]$SkipLocalVerification,

    [switch]$ExpectNativeApp,

    [switch]$IUnderstandThisStagesFirmware
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Resolve-AdbPathOrEmpty([string]$PathValue) {
    if ($PathValue -and (Test-Path -LiteralPath $PathValue)) {
        return (Resolve-Path -LiteralPath $PathValue).Path
    }

    $repoRoot = Split-Path -Parent $PSScriptRoot
    $repoAdb = Join-Path $repoRoot ".tools\platform-tools\adb.exe"
    if (Test-Path -LiteralPath $repoAdb) {
        return (Resolve-Path -LiteralPath $repoAdb).Path
    }

    $pathAdb = Get-Command adb -ErrorAction SilentlyContinue
    if ($pathAdb) {
        return $pathAdb.Source
    }

    return ""
}

function Test-AdbDevice([string]$AdbPath) {
    if (!$AdbPath) {
        return $false
    }
    $output = & $AdbPath devices 2>$null
    if ($LASTEXITCODE -ne 0) {
        return $false
    }
    return (($output -join "`n") -match "`tdevice\b")
}

function Invoke-LocalVerification([string]$PackagePath, [string]$BuildDir) {
    if ($SkipLocalVerification) {
        return
    }
    if (!$BuildDir) {
        $BuildDir = Split-Path -Parent $PackagePath
    }
    $verifyArgs = @(
        "tools\verify_r1_audiobook_build.py",
        "--out-dir", $BuildDir,
        "--upt-name", (Split-Path -Leaf $PackagePath),
        "--expected-version", $ExpectedVersion,
        "--expected-label", $ExpectedLabel,
        "--expect-audiobook-launcher-icon"
    )
    if ($ExpectNativeApp) {
        $verifyArgs += "--expect-native-app"
    } else {
        $verifyArgs += @("--require-db-maintenance", "--expect-batd-disabled")
    }
    & py -3 @verifyArgs
    if ($LASTEXITCODE -ne 0) {
        throw "local firmware verification failed; refusing to stage"
    }
}

function Get-VolumeDriveType([string]$RootPath) {
    $driveLetter = ([System.IO.Path]::GetPathRoot($RootPath)).Substring(0, 1)
    $volume = Get-Volume -DriveLetter $driveLetter -ErrorAction Stop
    return $volume.DriveType
}

function Get-CandidateSdRoots {
    if ($SdRoot) {
        $root = [System.IO.Path]::GetFullPath($SdRoot)
        if (-not $root.EndsWith("\")) {
            $root += "\"
        }
        if (!(Test-Path -LiteralPath $root)) {
            throw "SD root does not exist: $root"
        }
        if ((Get-VolumeDriveType $root) -ne "Removable") {
            throw "Refusing non-removable SD root: $root"
        }
        return @($root)
    }

    $roots = @()
    foreach ($volume in Get-Volume | Where-Object { $_.DriveLetter -and $_.DriveType -eq "Removable" -and $_.Size -gt 0 }) {
        $root = "$($volume.DriveLetter):\"
        $hasR1Shape = (Test-Path -LiteralPath (Join-Path $root "Music")) -or
            (Test-Path -LiteralPath (Join-Path $root "Audiobooks")) -or
            (Test-Path -LiteralPath (Join-Path $root "r1.upt"))
        if ($hasR1Shape) {
            $roots += $root
        }
    }
    return $roots
}

function Stage-ToSdRoot([string]$PackagePath, [string]$RootPath) {
    $packageItem = Get-Item -LiteralPath $PackagePath
    if ($packageItem.Length -lt 1048576) {
        throw "Refusing suspiciously small package ($($packageItem.Length) bytes): $PackagePath"
    }
    $localMd5 = (Get-FileHash -Algorithm MD5 -LiteralPath $PackagePath).Hash.ToLowerInvariant()
    $localSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $PackagePath).Hash.ToLowerInvariant()

    $final = Join-Path $RootPath "r1.upt"
    $tmp = Join-Path $RootPath "r1.upt.uploading"
    $backup = ""

    if (Test-Path -LiteralPath $tmp) {
        Remove-Item -Force -LiteralPath $tmp
    }

    if (Test-Path -LiteralPath $final) {
        $existingMd5 = (Get-FileHash -Algorithm MD5 -LiteralPath $final).Hash.ToLowerInvariant()
        if ($existingMd5 -ne $localMd5) {
            $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
            $backup = Join-Path $RootPath "r1.upt.previous-$timestamp.bak"
            Move-Item -LiteralPath $final -Destination $backup
        }
    }

    Copy-Item -LiteralPath $PackagePath -Destination $tmp
    $tmpItem = Get-Item -LiteralPath $tmp
    if ($tmpItem.Length -ne $packageItem.Length) {
        Remove-Item -Force -LiteralPath $tmp
        throw "temporary SD copy size mismatch: local=$($packageItem.Length) sd=$($tmpItem.Length)"
    }
    $tmpMd5 = (Get-FileHash -Algorithm MD5 -LiteralPath $tmp).Hash.ToLowerInvariant()
    if ($tmpMd5 -ne $localMd5) {
        Remove-Item -Force -LiteralPath $tmp
        throw "temporary SD copy MD5 mismatch: local=$localMd5 sd=$tmpMd5"
    }

    Move-Item -LiteralPath $tmp -Destination $final -Force
    $finalItem = Get-Item -LiteralPath $final
    $finalMd5 = (Get-FileHash -Algorithm MD5 -LiteralPath $final).Hash.ToLowerInvariant()
    $finalSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $final).Hash.ToLowerInvariant()
    if ($finalItem.Length -ne $packageItem.Length -or $finalMd5 -ne $localMd5 -or $finalSha256 -ne $localSha256) {
        throw "final SD package verification failed"
    }

    Write-Host "Staged firmware on SD root: $RootPath"
    Write-Host "Final file: $final"
    Write-Host "Bytes:      $($finalItem.Length)"
    Write-Host "MD5:        $finalMd5"
    Write-Host "SHA256:     $finalSha256"
    if ($backup) {
        Write-Host "Backup:     $backup"
    }
}

if (!$IUnderstandThisStagesFirmware) {
    throw "Refusing to stage firmware without -IUnderstandThisStagesFirmware"
}

$packagePath = Resolve-PathStrict $Package
$buildOutDirPath = if ($BuildOutDir) { Resolve-PathStrict $BuildOutDir } else { Split-Path -Parent $packagePath }

Invoke-LocalVerification $packagePath $buildOutDirPath

$adbPath = Resolve-AdbPathOrEmpty $Adb
if (Test-AdbDevice $adbPath) {
    Write-Host "ADB device detected; staging through adb_stage_verified_firmware.ps1."
    & (Join-Path $PSScriptRoot "adb_stage_verified_firmware.ps1") `
        -Adb $adbPath `
        -Package $packagePath `
        -BuildOutDir $buildOutDirPath `
        -ExpectedVersion $ExpectedVersion `
        -ExpectedLabel $ExpectedLabel `
        -ExpectNativeApp:$ExpectNativeApp `
        -RequireBootAdb:$ExpectNativeApp `
        -ExpectAudiobookLauncherIcon `
        -RequireDbMaintenance:(!$ExpectNativeApp) `
        -IUnderstandThisStagesFirmware `
        -SkipLocalVerification:$SkipLocalVerification
    exit $LASTEXITCODE
}

$candidates = @(Get-CandidateSdRoots)
if ($candidates.Count -eq 0) {
    throw "No ADB device and no single removable SD root with Music, Audiobooks, or r1.upt was found."
}
if ($candidates.Count -gt 1) {
    throw "Multiple possible SD roots found: $($candidates -join ', '). Re-run with -SdRoot <drive>."
}

Stage-ToSdRoot $packagePath $candidates[0]
