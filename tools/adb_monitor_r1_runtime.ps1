param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [int]$DurationMinutes = 60,

    [Parameter(Mandatory=$false)]
    [int]$IntervalSeconds = 60,

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

if ($DurationMinutes -le 0) {
    throw "DurationMinutes must be positive"
}
if ($IntervalSeconds -le 0) {
    throw "IntervalSeconds must be positive"
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$adbPath = Require-Path $Adb
if (-not $OutDir) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutDir = Join-Path $repoRoot "work\runtime-monitor\$stamp"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$devicesPath = Join-Path $OutDir "adb-devices.txt"
& $adbPath devices | Tee-Object -FilePath $devicesPath | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

$samplePath = Join-Path $OutDir "samples.txt"
$sampleCommand = @'
echo '=== sample ==='
date
echo '--- version ---'
cat /etc/r1_audiobook_version 2>/dev/null || true
echo '--- uptime/load ---'
cat /proc/uptime 2>/dev/null || true
cat /proc/loadavg 2>/dev/null || true
echo '--- battery ---'
for d in /sys/class/power_supply/*; do
  [ -d "$d" ] || continue
  echo "supply=$(basename "$d")"
  for f in type status capacity voltage_now current_now charge_now energy_now temp present online; do
    [ -r "$d/$f" ] && echo "$f=$(cat "$d/$f" 2>/dev/null)"
  done
done
echo '--- memory ---'
cat /proc/meminfo 2>/dev/null | head -20 || true
echo '--- processes ---'
ps | grep -E 'hiby_player|r1_audiobook|db_watch|dmrd|adbd' | grep -v grep || true
echo '--- top ---'
top -n 1 2>/dev/null | head -20 || true
echo '--- current user.ini path slot ---'
dd if=/usr/data/user.ini bs=1 skip=40 count=256 2>/dev/null | xxd -p -c 64 || true
echo '--- audiobook logs tail ---'
tail -20 /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true
tail -20 /usr/data/audiobooks/db-watch.log 2>/dev/null || true
echo
'@

$endTime = (Get-Date).AddMinutes($DurationMinutes)
$sampleCount = 0
Write-Host "Writing monitor samples to: $samplePath"
while ((Get-Date) -lt $endTime) {
    $sampleCount++
    Write-Host "Sample $sampleCount at $(Get-Date -Format s)"
    & $adbPath shell $sampleCommand | Tee-Object -FilePath $samplePath -Append | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "adb shell sample failed"
    }
    if ((Get-Date) -lt $endTime) {
        Start-Sleep -Seconds $IntervalSeconds
    }
}

Write-Host "Runtime monitor bundle: $OutDir"
