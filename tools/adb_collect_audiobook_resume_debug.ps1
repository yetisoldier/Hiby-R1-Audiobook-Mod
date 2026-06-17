param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "",

    [Parameter(Mandatory=$false)]
    [string]$RemoteBase = "/usr/data/audiobooks",

    [Parameter(Mandatory=$false)]
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

function Require-Path([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Resolve-AdbPath([string]$PathValue) {
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

    if ($PathValue) {
        throw "Missing adb path: $PathValue"
    }
    throw "ADB not found. Install platform-tools, add adb to PATH, or place adb.exe at .tools\platform-tools\adb.exe."
}

function Invoke-AdbText([string]$Command) {
    & $adbPath shell $Command
    if ($LASTEXITCODE -ne 0) {
        throw "adb shell failed: $Command"
    }
}

function Pull-IfExists([string]$Remote, [string]$Local) {
    $existsOutput = & $adbPath shell "[ -e '$Remote' ] && echo yes || true"
    $exists = if ($null -eq $existsOutput) { "" } else { ($existsOutput -join "`n").Trim() }
    if ($exists -eq "yes") {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Local) | Out-Null
        & $adbPath pull $Remote $Local | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "failed to pull $Remote"
        }
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$adbPath = Resolve-AdbPath $Adb
if (-not $OutDir) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutDir = Join-Path $repoRoot "work\resume-debug\$stamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

& $adbPath devices | Tee-Object -FilePath (Join-Path $OutDir "adb-devices.txt") | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

$summary = @"
echo '--- date ---'
date
echo '--- version ---'
cat /etc/r1_audiobook_version 2>/dev/null || true
echo '--- df ---'
df -h /usr/data /usr/data/mnt/sd_0 2>/dev/null || true
echo '--- uptime/load ---'
cat /proc/uptime 2>/dev/null || true
cat /proc/loadavg 2>/dev/null || true
echo '--- memory ---'
cat /proc/meminfo 2>/dev/null | head -40 || true
echo '--- processes ---'
ps | grep -E 'hiby_player|r1_audiobook|db_watch' | grep -v grep || true
echo '--- top ---'
top -n 1 2>/dev/null | head -30 || true
echo '--- kernel messages tail ---'
dmesg 2>/dev/null | tail -120 || true
echo '--- current user.ini path slot hex ---'
dd if=/usr/data/user.ini bs=1 skip=40 count=512 2>/dev/null | xxd -p -c 64 || true
echo '--- resume records ---'
ls -l '$RemoteBase/resume.d' 2>/dev/null || true
echo '--- catalog header ---'
head -5 '$RemoteBase/catalog.tsv' 2>/dev/null || true
echo '--- daemon log tail ---'
tail -120 '$RemoteBase/resume-daemon.log' 2>/dev/null || true
echo '--- daemon stdout tail ---'
tail -80 '$RemoteBase/resume-daemon.stdout.log' 2>/dev/null || true
echo '--- db watch log tail ---'
tail -80 '$RemoteBase/db-watch.log' 2>/dev/null || true
echo '--- db maint log tail ---'
tail -80 '$RemoteBase/db-maint.log' 2>/dev/null || true
echo '--- db maint stdout tail ---'
tail -80 '$RemoteBase/db-maint.stdout.log' 2>/dev/null || true
"@

& $adbPath shell $summary | Tee-Object -FilePath (Join-Path $OutDir "summary.txt") | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "failed to collect summary"
}

Pull-IfExists "$RemoteBase/resume-daemon.log" (Join-Path $OutDir "resume-daemon.log")
Pull-IfExists "$RemoteBase/resume-daemon.stdout.log" (Join-Path $OutDir "resume-daemon.stdout.log")
Pull-IfExists "$RemoteBase/db-watch.log" (Join-Path $OutDir "db-watch.log")
Pull-IfExists "$RemoteBase/db-maint.log" (Join-Path $OutDir "db-maint.log")
Pull-IfExists "$RemoteBase/db-maint.stdout.log" (Join-Path $OutDir "db-maint.stdout.log")
Pull-IfExists "$RemoteBase/catalog.tsv" (Join-Path $OutDir "catalog.tsv")
Pull-IfExists "$RemoteBase/catalog-albums.txt" (Join-Path $OutDir "catalog-albums.txt")
Pull-IfExists "$RemoteBase/catalog-books.tsv" (Join-Path $OutDir "catalog-books.tsv")
Pull-IfExists "$RemoteBase/catalog-view-title.tsv" (Join-Path $OutDir "catalog-view-title.tsv")
Pull-IfExists "$RemoteBase/catalog-view-author.tsv" (Join-Path $OutDir "catalog-view-author.tsv")
Pull-IfExists "$RemoteBase/catalog-view-series.tsv" (Join-Path $OutDir "catalog-view-series.tsv")
Pull-IfExists "$RemoteBase/resume.d" (Join-Path $OutDir "resume.d")
Pull-IfExists "/usr/data/user.ini" (Join-Path $OutDir "user.ini")

Write-Host "Debug bundle: $OutDir"
