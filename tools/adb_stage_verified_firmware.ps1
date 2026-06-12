param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [string]$Package = "work\audiobook-firmware-1.6.15-dbwatch-lock-candidate\r1-audiobooks-1.6.15-audiobook.upt",

    [Parameter(Mandatory=$false)]
    [string]$BuildOutDir = "",

    [Parameter(Mandatory=$false)]
    [string]$VerifyScript = "tools\verify_r1_audiobook_build.py",

    [Parameter(Mandatory=$false)]
    [string]$StockRootfs = "work\original\rootfs.squashfs",

    [Parameter(Mandatory=$false)]
    [string]$ExpectedVersion = "1.6.15-audiobook",

    [Parameter(Mandatory=$false)]
    [string]$ExpectedLabel = "HiBy R1 Audiobook FW 1.6.15",

    [Parameter(Mandatory=$false)]
    [string]$RemoteFinal = "/usr/data/mnt/sd_0/r1.upt",

    [switch]$ExpectCurrentHashes,

    [switch]$RequireDbMaintenance = $true,

    [switch]$SkipLocalVerification,

    [switch]$NoBackupExistingFinal,

    [switch]$IUnderstandThisStagesFirmware
)

$ErrorActionPreference = "Stop"

$KnownBadMd5 = @(
    # Flashed on 2026-06-09; rootfs repack left hiby_player non-executable.
    "2dc1152f096e84b3b8b52f809fc30e59",
    # Flashed on 2026-06-09; update reported success but booted to a black screen.
    "3bed523d5843522186164029139db7b1"
)

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Get-RemoteSha256OrEmpty([string]$RemotePath) {
    $output = & $adbPath shell "sha256sum '$RemotePath' 2>/dev/null || true"
    if ($LASTEXITCODE -ne 0) {
        return ""
    }
    $joined = $output -join "`n"
    if ($joined -match "^[0-9a-fA-F]{64}\b") {
        return $Matches[0].ToLowerInvariant()
    }
    return ""
}

function Get-RemoteSizeOrEmpty([string]$RemotePath) {
    $output = & $adbPath shell "wc -c < '$RemotePath' 2>/dev/null || true"
    if ($LASTEXITCODE -eq 0) {
        $joined = $output -join "`n"
        if ($joined -match "([0-9]+)") {
            return $Matches[1]
        }
    }

    $output = & $adbPath shell "stat -c %s '$RemotePath' 2>/dev/null || true"
    if ($LASTEXITCODE -eq 0) {
        $joined = $output -join "`n"
        if ($joined -match "([0-9]+)") {
            return $Matches[1]
        }
    }

    $output = & $adbPath shell "ls -l '$RemotePath' 2>/dev/null || true"
    if ($LASTEXITCODE -eq 0) {
        foreach ($line in $output) {
            if ($line -match "^\S+\s+\S+\s+\S+\s+\S+\s+([0-9]+)\s+") {
                return $Matches[1]
            }
        }
    }
    return ""
}

function Remove-RemoteIfExists([string]$RemotePath) {
    & $adbPath shell "rm -f '$RemotePath' 2>/dev/null || true" | Out-Null
}

if (!$IUnderstandThisStagesFirmware) {
    throw "Refusing to stage firmware without -IUnderstandThisStagesFirmware"
}

$adbPath = Resolve-PathStrict $Adb
$packagePath = Resolve-PathStrict $Package
$packageInfo = Get-Item -LiteralPath $packagePath
if ($packageInfo.Length -lt 1048576) {
    throw "Refusing to stage suspiciously small package ($($packageInfo.Length) bytes): $packagePath"
}
$localMd5 = (Get-FileHash -Algorithm MD5 -LiteralPath $packagePath).Hash.ToLowerInvariant()
$localSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $packagePath).Hash.ToLowerInvariant()

if ($KnownBadMd5 -contains $localMd5) {
    throw "Refusing to stage known-bad package MD5 $localMd5"
}

if (!$SkipLocalVerification) {
    $verifyScriptPath = Resolve-PathStrict $VerifyScript
    $stockRootfsPath = Resolve-PathStrict $StockRootfs
    if (!$BuildOutDir) {
        $BuildOutDir = Split-Path -Parent $packagePath
    }
    $buildOutDirPath = Resolve-PathStrict $BuildOutDir
    $uptName = Split-Path -Leaf $packagePath
    $verifyArgs = @(
        $verifyScriptPath,
        "--out-dir", $buildOutDirPath,
        "--upt-name", $uptName,
        "--stock-rootfs", $stockRootfsPath,
        "--expected-version", $ExpectedVersion,
        "--expected-label", $ExpectedLabel
    )
    if ($ExpectCurrentHashes) {
        $verifyArgs += "--expect-current-hashes"
    }
    if ($RequireDbMaintenance) {
        $verifyArgs += "--require-db-maintenance"
    }
    python @verifyArgs
    if ($LASTEXITCODE -ne 0) {
        throw "local firmware verification failed; refusing to stage"
    }
}

$remoteFinalDir = ($RemoteFinal -replace '/[^/]+$', '')
if ([string]::IsNullOrWhiteSpace($remoteFinalDir) -or $remoteFinalDir -eq $RemoteFinal) {
    throw "RemoteFinal must be an absolute path with a parent directory: $RemoteFinal"
}
$remoteTmpDir = "$remoteFinalDir/.r1-audiobook-staging"
$remoteTmp = "$remoteTmpDir/r1.upt.uploading"

& $adbPath devices | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

Write-Host "Local package: $packagePath"
Write-Host "Local bytes:   $($packageInfo.Length)"
Write-Host "Local MD5:     $localMd5"
Write-Host "Local SHA256:  $localSha256"
Write-Host "Remote final:  $RemoteFinal"

& $adbPath shell "mkdir -p '$remoteTmpDir' && rm -f '$remoteTmp'"
if ($LASTEXITCODE -ne 0) {
    throw "failed to prepare temp package path"
}

& $adbPath push $packagePath $remoteTmp | Out-Host
if ($LASTEXITCODE -ne 0) {
    Remove-RemoteIfExists $remoteTmp
    throw "adb push failed"
}

$remoteSize = Get-RemoteSizeOrEmpty $remoteTmp
if ($remoteSize -ne "$($packageInfo.Length)") {
    Remove-RemoteIfExists $remoteTmp
    throw "remote temp size mismatch: local=$($packageInfo.Length) remote=$remoteSize"
}

$remoteMd5Output = & $adbPath shell "sync; md5sum '$remoteTmp'"
if ($LASTEXITCODE -ne 0) {
    Remove-RemoteIfExists $remoteTmp
    throw "remote temp md5sum failed"
}
$remoteMd5 = ($remoteMd5Output -join "`n").Split()[0].ToLowerInvariant()
if ($remoteMd5 -ne $localMd5) {
    Remove-RemoteIfExists $remoteTmp
    throw "remote temp MD5 mismatch: local=$localMd5 remote=$remoteMd5"
}

$remoteSha256 = Get-RemoteSha256OrEmpty $remoteTmp
if ($remoteSha256) {
    if ($remoteSha256 -ne $localSha256) {
        Remove-RemoteIfExists $remoteTmp
        throw "remote temp SHA256 mismatch: local=$localSha256 remote=$remoteSha256"
    }
} else {
    Write-Warning "remote sha256sum unavailable for temp package; continuing after byte count and MD5 verification"
}

$remoteBackup = ""
$remoteFinalExistsOutput = & $adbPath shell "if [ -e '$RemoteFinal' ]; then echo yes; else echo no; fi"
if ($LASTEXITCODE -ne 0) {
    throw "remote final existence check failed"
}
$remoteFinalExists = (($remoteFinalExistsOutput -join "`n").Trim() -eq "yes")
if ($remoteFinalExists -and !$NoBackupExistingFinal) {
    $existingMd5Output = & $adbPath shell "md5sum '$RemoteFinal' 2>/dev/null || true"
    if ($LASTEXITCODE -ne 0) {
        throw "remote existing final md5sum failed"
    }
    $existingMd5Text = $existingMd5Output -join "`n"
    $existingMd5 = ""
    if ($existingMd5Text -match "^[0-9a-fA-F]{32}\b") {
        $existingMd5 = $Matches[0].ToLowerInvariant()
    }
    if (!$existingMd5 -or $existingMd5 -ne $localMd5) {
        $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $remoteBackup = "$RemoteFinal.previous-$timestamp.bak"
        Write-Host "Backing up existing remote final to: $remoteBackup"
        if ($existingMd5) {
            Write-Host "Existing remote final MD5: $existingMd5"
        }
        & $adbPath shell "mv '$RemoteFinal' '$remoteBackup'; sync"
        if ($LASTEXITCODE -ne 0) {
            throw "remote final backup failed"
        }
    } else {
        Write-Host "Existing remote final already matches local package; backup skipped."
    }
}

& $adbPath shell "mv '$remoteTmp' '$RemoteFinal'; sync"
if ($LASTEXITCODE -ne 0) {
    Remove-RemoteIfExists $remoteTmp
    throw "remote final rename failed"
}

$remoteFinalSize = Get-RemoteSizeOrEmpty $RemoteFinal
if ($remoteFinalSize -ne "$($packageInfo.Length)") {
    throw "remote final size mismatch: local=$($packageInfo.Length) remote=$remoteFinalSize"
}

$remoteFinalMd5Output = & $adbPath shell "md5sum '$RemoteFinal'"
if ($LASTEXITCODE -ne 0) {
    throw "remote final md5sum failed"
}
$remoteFinalMd5 = ($remoteFinalMd5Output -join "`n").Split()[0].ToLowerInvariant()
if ($remoteFinalMd5 -ne $localMd5) {
    throw "remote final MD5 mismatch: local=$localMd5 remote=$remoteFinalMd5"
}

$remoteFinalSha256 = Get-RemoteSha256OrEmpty $RemoteFinal
if ($remoteFinalSha256) {
    if ($remoteFinalSha256 -ne $localSha256) {
        throw "remote final SHA256 mismatch: local=$localSha256 remote=$remoteFinalSha256"
    }
} else {
    Write-Warning "remote sha256sum unavailable for final package; continuing after byte count and MD5 verification"
}

Write-Host "Remote final bytes: $remoteFinalSize"
Write-Host "Remote final MD5:   $remoteFinalMd5"
if ($remoteFinalSha256) {
    Write-Host "Remote final SHA256: $remoteFinalSha256"
}
if ($remoteBackup) {
    Write-Host "Previous remote final backup: $remoteBackup"
}
& $adbPath shell "ls -l '$RemoteFinal'"

Write-Host "Staged firmware package successfully."
