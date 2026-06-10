param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$true)]
    [string]$Database,

    [Parameter(Mandatory=$true)]
    [string]$Catalog,

    [Parameter(Mandatory=$false)]
    [string]$CheckScript = "tools\check_audiobook_release_state.py",

    [Parameter(Mandatory=$false)]
    [string]$RemoteDb = "/usr/data/usrlocal_media.db",

    [Parameter(Mandatory=$false)]
    [string]$RemoteCatalog = "/usr/data/audiobooks/catalog.tsv",

    [Parameter(Mandatory=$false)]
    [string]$BackupOutDir = "work\release-db-install-backups",

    [Parameter(Mandatory=$false)]
    [string]$RemoteBackupSdRoot = "/usr/data/mnt/sd_0/.r1-audiobook-backups",

    [switch]$RestartResumeDaemon,

    [switch]$RebootAfterInstall,

    [switch]$MoveRemoteBackupsToSd,

    [switch]$IUnderstandThisModifiesDevice
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Get-RemoteMd5([string]$RemotePath) {
    $output = & $adbPath shell "md5sum '$RemotePath' 2>/dev/null || true"
    if ($LASTEXITCODE -ne 0) {
        throw "remote md5sum failed for $RemotePath"
    }
    $text = $output -join "`n"
    if ($text -match "^[0-9a-fA-F]{32}\b") {
        return $Matches[0].ToLowerInvariant()
    }
    return ""
}

function Assert-RemoteFileMatches([string]$RemotePath, [string]$LocalPath, [string]$ExpectedMd5) {
    $localInfo = Get-Item -LiteralPath $LocalPath
    $remoteSizeOutput = & $adbPath shell "wc -c < '$RemotePath'"
    if ($LASTEXITCODE -ne 0) {
        throw "remote size check failed for $RemotePath"
    }
    $remoteSize = (($remoteSizeOutput -join "`n") -replace '[^0-9]', '')
    if ($remoteSize -ne "$($localInfo.Length)") {
        throw "remote size mismatch for $RemotePath`: local=$($localInfo.Length) remote=$remoteSize"
    }
    $remoteMd5 = Get-RemoteMd5 $RemotePath
    if ($remoteMd5 -ne $ExpectedMd5) {
        throw "remote MD5 mismatch for $RemotePath`: local=$ExpectedMd5 remote=$remoteMd5"
    }
}

function Get-RemoteLeaf([string]$RemotePath) {
    return ($RemotePath -replace '^.*/', '')
}

function Invoke-AdbAllowingProgress([string[]]$Arguments) {
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "SilentlyContinue"
    try {
        & $adbPath @Arguments | Out-Host
        return $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
}

if (!$IUnderstandThisModifiesDevice) {
    throw "Refusing to install release DB without -IUnderstandThisModifiesDevice"
}

$adbPath = Resolve-PathStrict $Adb
$dbPath = Resolve-PathStrict $Database
$catalogPath = Resolve-PathStrict $Catalog
$checkScriptPath = Resolve-PathStrict $CheckScript

python $checkScriptPath $dbPath --catalog $catalogPath --expect-audiobooks
if ($LASTEXITCODE -ne 0) {
    throw "release-state database check failed; refusing to install"
}

$dbMd5 = (Get-FileHash -Algorithm MD5 -LiteralPath $dbPath).Hash.ToLowerInvariant()
$catalogMd5 = (Get-FileHash -Algorithm MD5 -LiteralPath $catalogPath).Hash.ToLowerInvariant()
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backupDir = Join-Path (Resolve-Path -LiteralPath (Get-Location).Path).Path (Join-Path $BackupOutDir $stamp)
New-Item -ItemType Directory -Force -Path $backupDir | Out-Null

& $adbPath devices | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

Write-Host "Local DB:      $dbPath"
Write-Host "Local DB MD5:  $dbMd5"
Write-Host "Local catalog: $catalogPath"
Write-Host "Catalog MD5:   $catalogMd5"
Write-Host "Backup dir:    $backupDir"

$pullDbCode = Invoke-AdbAllowingProgress @("pull", $RemoteDb, (Join-Path $backupDir "usrlocal_media.db.before"))
if ($pullDbCode -ne 0) {
    throw "failed to pull existing media DB backup"
}
& $adbPath shell "if [ -e '$RemoteCatalog' ]; then echo present; else echo missing; fi" | Tee-Object -FilePath (Join-Path $backupDir "remote-catalog-presence.txt") | Out-Host
$catalogPresence = Get-Content -LiteralPath (Join-Path $backupDir "remote-catalog-presence.txt") -Raw
if ($catalogPresence -match "present") {
    $pullCatalogCode = Invoke-AdbAllowingProgress @("pull", $RemoteCatalog, (Join-Path $backupDir "catalog.tsv.before"))
    if ($pullCatalogCode -ne 0) {
        throw "failed to pull existing catalog backup"
    }
}

$remoteBackupDb = "$RemoteDb.pre-release-$stamp.bak"
$remoteBackupCatalog = "$RemoteCatalog.pre-release-$stamp.bak"
$remoteTmpDb = "$RemoteDb.uploading-$stamp"
$remoteTmpCatalog = "$RemoteCatalog.uploading-$stamp"

& $adbPath shell "mkdir -p '/usr/data/audiobooks' '/usr/data/audiobooks/release-backups'"
if ($LASTEXITCODE -ne 0) {
    throw "failed to create remote audiobook directories"
}

& $adbPath push $dbPath $remoteTmpDb | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "failed to push DB temp file"
}
Assert-RemoteFileMatches $remoteTmpDb $dbPath $dbMd5

& $adbPath push $catalogPath $remoteTmpCatalog | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "failed to push catalog temp file"
}
Assert-RemoteFileMatches $remoteTmpCatalog $catalogPath $catalogMd5

& $adbPath shell "cp -p '$RemoteDb' '$remoteBackupDb' && if [ -e '$RemoteCatalog' ]; then cp -p '$RemoteCatalog' '$remoteBackupCatalog'; fi && mv '$remoteTmpDb' '$RemoteDb' && mv '$remoteTmpCatalog' '$RemoteCatalog' && tail -n +2 '$RemoteCatalog' | cut -f1 | sed '/^$/d' | sort -u > /usr/data/audiobooks/catalog-roots.txt && tail -n +2 '$RemoteCatalog' | cut -f7 | sed '/^$/d' | sort -u > /usr/data/audiobooks/catalog-albums.txt && sync"
if ($LASTEXITCODE -ne 0) {
    throw "remote DB/catalog install failed"
}

Assert-RemoteFileMatches $RemoteDb $dbPath $dbMd5
Assert-RemoteFileMatches $RemoteCatalog $catalogPath $catalogMd5

Write-Host "Installed release DB and catalog."
Write-Host "Remote DB backup:      $remoteBackupDb"
Write-Host "Remote catalog backup: $remoteBackupCatalog"

if ($MoveRemoteBackupsToSd) {
    $remoteBackupSdDir = "$RemoteBackupSdRoot/release-db-$stamp"
    & $adbPath shell "mkdir -p '$remoteBackupSdDir'"
    if ($LASTEXITCODE -ne 0) {
        throw "failed to create SD backup directory: $remoteBackupSdDir"
    }

    $localDbBackup = Join-Path $backupDir "usrlocal_media.db.before"
    $localDbBackupMd5 = (Get-FileHash -Algorithm MD5 -LiteralPath $localDbBackup).Hash.ToLowerInvariant()
    $sdDbBackup = "$remoteBackupSdDir/$(Get-RemoteLeaf $remoteBackupDb)"
    & $adbPath push $localDbBackup "$sdDbBackup.tmp" | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to push SD DB backup"
    }
    Assert-RemoteFileMatches "$sdDbBackup.tmp" $localDbBackup $localDbBackupMd5
    & $adbPath shell "mv '$sdDbBackup.tmp' '$sdDbBackup'"
    if ($LASTEXITCODE -ne 0) {
        throw "failed to finalize SD DB backup"
    }
    Assert-RemoteFileMatches $sdDbBackup $localDbBackup $localDbBackupMd5

    $rmInternalBackups = "rm -f '$remoteBackupDb'"
    $localCatalogBackup = Join-Path $backupDir "catalog.tsv.before"
    if (Test-Path -LiteralPath $localCatalogBackup) {
        $localCatalogBackupMd5 = (Get-FileHash -Algorithm MD5 -LiteralPath $localCatalogBackup).Hash.ToLowerInvariant()
        $sdCatalogBackup = "$remoteBackupSdDir/$(Get-RemoteLeaf $remoteBackupCatalog)"
        & $adbPath push $localCatalogBackup "$sdCatalogBackup.tmp" | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "failed to push SD catalog backup"
        }
        Assert-RemoteFileMatches "$sdCatalogBackup.tmp" $localCatalogBackup $localCatalogBackupMd5
        & $adbPath shell "mv '$sdCatalogBackup.tmp' '$sdCatalogBackup'"
        if ($LASTEXITCODE -ne 0) {
            throw "failed to finalize SD catalog backup"
        }
        Assert-RemoteFileMatches $sdCatalogBackup $localCatalogBackup $localCatalogBackupMd5
        $rmInternalBackups = "$rmInternalBackups '$remoteBackupCatalog'"
        Write-Host "SD catalog backup:     $sdCatalogBackup"
    }

    & $adbPath shell "$rmInternalBackups && sync"
    if ($LASTEXITCODE -ne 0) {
        throw "failed to remove internal remote backup copy after SD verification"
    }
    Write-Host "SD DB backup:          $sdDbBackup"
    Write-Host "Moved verified remote backups to SD to save internal /usr/data space."
}

if ($RestartResumeDaemon -and !$RebootAfterInstall) {
    & $adbPath shell "/etc/init.d/S91audiobook_resume.sh start"
    if ($LASTEXITCODE -ne 0) {
        throw "failed to restart resume daemon"
    }
    Write-Host "Restarted resume daemon."
}

if ($RebootAfterInstall) {
    Write-Host "Rebooting device. ADB may need to be manually re-enabled afterward."
    & $adbPath shell "sync; reboot"
}
else {
    Write-Host "Device not rebooted. Reboot before final UI verification so hiby_player reloads the DB cleanly."
}
