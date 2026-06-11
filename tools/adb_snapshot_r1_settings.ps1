param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [string]$Label = "snapshot",

    [Parameter(Mandatory=$false)]
    [string]$OutDir = "work\settings-snapshots",

    [Parameter(Mandatory=$false)]
    [string]$CompareTo
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

function Invoke-AdbShellCapture([string]$FileName, [string]$CommandText) {
    $targetPath = Join-Path $snapshotDir $FileName
    $output = & $adbPath shell $CommandText 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -LiteralPath $targetPath -Encoding UTF8
    if ($exitCode -ne 0) {
        throw "adb shell capture failed for $FileName with exit code $exitCode"
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$adbPath = Resolve-PathStrict $Adb
$safeLabel = Convert-ToSafeName $Label
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$snapshotRoot = Join-Path $repoRoot $OutDir
$snapshotDir = Join-Path $snapshotRoot "$timestamp-$safeLabel"
$filesDir = Join-Path $snapshotDir "files"
New-Item -ItemType Directory -Force -Path $filesDir | Out-Null

$devices = & $adbPath devices 2>&1
$devices | Set-Content -LiteralPath (Join-Path $snapshotDir "adb-devices.txt") -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

$deviceInfoCommand = @'
echo "date=$(date 2>/dev/null)"
echo "kernel=$(uname -a 2>/dev/null)"
echo
echo "== audiobook version =="
cat /etc/r1_audiobook_version 2>/dev/null || true
echo
echo "== boot adb files =="
ls -l /etc/init.d/T90adb /etc/init.d/S90adb /usr/data/disableadb 2>/dev/null || true
echo
echo "== process list =="
ps 2>/dev/null || true
echo
echo "== mounts =="
mount 2>/dev/null || true
'@
Invoke-AdbShellCapture "device-info.txt" $deviceInfoCommand

$manifestCommand = @'
for base in /usr/data /data; do
    if [ -d "$base" ]; then
        echo "## $base"
        find "$base" -maxdepth 4 -type f 2>/dev/null | sort | while IFS= read -r f; do
            size=$(wc -c < "$f" 2>/dev/null | tr -d " ")
            hash=$(md5sum "$f" 2>/dev/null | awk "{print \$1}")
            lsline=$(ls -l "$f" 2>/dev/null)
            printf "%s\t%s\t%s\t%s\n" "$size" "$hash" "$lsline" "$f"
        done
    else
        echo "## missing $base"
    fi
done
'@
Invoke-AdbShellCapture "settings-manifest.tsv" $manifestCommand

$candidateCommand = @'
for base in /usr/data /data; do
    if [ -d "$base" ]; then
        find "$base" -maxdepth 4 -type f 2>/dev/null | while IFS= read -r f; do
            lower=$(echo "$f" | tr "A-Z" "a-z")
            size=$(wc -c < "$f" 2>/dev/null | tr -d " ")
            case "$lower" in
                *setting*|*config*|*cfg|*.ini|*.conf|*.json|*usb*|*adb*|*dev*|*mode*|*user*|*sys*)
                    if [ -n "$size" ] && [ "$size" -le 262144 ]; then
                        printf "%s\t%s\n" "$size" "$f"
                    fi
                    ;;
            esac
        done
    fi
done
'@
Invoke-AdbShellCapture "candidate-files.tsv" $candidateCommand

$pullLog = Join-Path $snapshotDir "pull.log"
"# Pulled small settings/config candidates" | Set-Content -LiteralPath $pullLog -Encoding UTF8
$candidates = Get-Content -LiteralPath (Join-Path $snapshotDir "candidate-files.tsv") -ErrorAction SilentlyContinue
foreach ($line in $candidates) {
    if ([string]::IsNullOrWhiteSpace($line)) {
        continue
    }
    $parts = $line -split "`t", 2
    if ($parts.Count -ne 2) {
        continue
    }
    $remotePath = $parts[1].Trim()
    if ([string]::IsNullOrWhiteSpace($remotePath)) {
        continue
    }
    $trimmed = $remotePath.TrimStart([char[]]"/")
    $localName = $trimmed -replace '[\\/:*?"<>| ]+', "_"
    $destPath = Join-Path $filesDir $localName
    "PULL $remotePath -> $destPath" | Add-Content -LiteralPath $pullLog
    $pullOutput = & $adbPath pull $remotePath $destPath 2>&1
    $pullOutput | Add-Content -LiteralPath $pullLog
    if ($LASTEXITCODE -ne 0) {
        "WARN pull failed for $remotePath" | Add-Content -LiteralPath $pullLog
    }
}

if ($CompareTo) {
    $comparePath = Resolve-PathStrict $CompareTo
    $compareScript = Join-Path $PSScriptRoot "compare_r1_settings_snapshots.ps1"
    if (Test-Path -LiteralPath $compareScript) {
        powershell -NoProfile -ExecutionPolicy Bypass `
            -File $compareScript `
            -Before $comparePath `
            -After $snapshotDir | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "settings snapshot comparison failed"
        }
    }
}

Write-Host "Snapshot saved to $snapshotDir"
Write-Host "Use a before/after pair around a UI setting change to identify persisted settings."
