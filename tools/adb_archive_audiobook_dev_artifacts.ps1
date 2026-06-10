param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [string]$RemoteBase = "/usr/data/audiobooks",

    [Parameter(Mandatory=$false)]
    [string]$RemoteArchiveRoot = "",

    [switch]$IUnderstandThisMovesDeviceFiles
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

$adbPath = Resolve-PathStrict $Adb
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($RemoteArchiveRoot)) {
    $archiveDir = "$RemoteBase/dev-archive-$stamp"
}
else {
    $archiveDir = "$($RemoteArchiveRoot.TrimEnd('/'))/dev-archive-$stamp"
}

$devArtifacts = @(
    "debug-daemon.out",
    "debug-daemon.pid",
    "dmr-probe.out",
    "helper-current.out",
    "helper-current.strace",
    "mem-pos-near.bin",
    "player-restart.out",
    "player-restart2.out",
    "position-watch-holidays-on-ice-2008.nohup.log",
    "position-watch-holidays-on-ice-2008.pid",
    "position-watch-holidays.loop.log",
    "position-watch-holidays.nohup.log",
    "position-watch-holidays.pid",
    "ptrwins",
    "r1_audiobook_resume_daemon.syntax-test.sh",
    "resume-daemon.testpid",
    "resume-daemon.trace",
    "scan_skip_runtime_patch.json",
    "tracklist-window.bin",
    "user.ini.before-stock-audiobook-last-clear"
)

& $adbPath devices | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

$present = @()
foreach ($name in $devArtifacts) {
    $remote = "$RemoteBase/$name"
    $exists = (& $adbPath shell "if [ -e '$remote' ]; then echo yes; else echo no; fi") -join "`n"
    if ($exists.Trim() -eq "yes") {
        $present += $name
    }
}

if (!$present) {
    Write-Host "No known development artifacts found under $RemoteBase."
    exit 0
}

Write-Host "Known development artifacts under ${RemoteBase}:"
foreach ($name in $present) {
    Write-Host "  $name"
}

if (!$IUnderstandThisMovesDeviceFiles) {
    Write-Host ""
    Write-Host "Dry run only. Re-run with -IUnderstandThisMovesDeviceFiles to move these into:"
    Write-Host "  $archiveDir"
    exit 0
}

& $adbPath shell "mkdir -p '$archiveDir'"
if ($LASTEXITCODE -ne 0) {
    throw "failed to create archive directory"
}

foreach ($name in $present) {
    $remote = "$RemoteBase/$name"
    & $adbPath shell "mv '$remote' '$archiveDir/'"
    if ($LASTEXITCODE -ne 0) {
        throw "failed to archive $remote"
    }
}

Write-Host "Moved $($present.Count) development artifacts to $archiveDir"
