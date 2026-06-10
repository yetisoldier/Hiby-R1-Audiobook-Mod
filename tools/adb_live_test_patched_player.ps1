param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [string]$Player = "work\audiobook-firmware\hiby_player.audiobooks",

    [Parameter(Mandatory=$false)]
    [switch]$StartPatched,

    [Parameter(Mandatory=$false)]
    [switch]$RestoreStock,

    [Parameter(Mandatory=$false)]
    [switch]$IUnderstandThisRestartsUi
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Invoke-Adb([string[]]$Arguments) {
    & $Adb @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb failed: $($Arguments -join ' ')"
    }
}

$stopPlayerScript = 'ps | sed -n ''/hiby_player/ { /\/bin\/sh -c/d; /grep/d; s/^ *\([0-9][0-9]*\).*/\1/p; }'' | while read p; do kill -9 "$p" 2>/dev/null; done; sleep 1'

if ($StartPatched -and $RestoreStock) {
    throw "Choose only one mode: -StartPatched or -RestoreStock."
}

if (!$StartPatched -and !$RestoreStock) {
    throw "Choose -StartPatched or -RestoreStock."
}

$adbPath = Resolve-PathStrict $Adb
$Adb = $adbPath

if ($StartPatched) {
    if (!$IUnderstandThisRestartsUi) {
        throw "Starting the patched player restarts the R1 UI. Re-run with -IUnderstandThisRestartsUi after explicit approval."
    }

    $playerPath = Resolve-PathStrict $Player
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $remoteDir = "/usr/data/codex_audiobook_test_$stamp"
    $remotePlayer = "$remoteDir/hiby_player"
    $localState = "work\audiobook-firmware\last-live-test-remote.txt"

    Invoke-Adb -Arguments @("shell", "mkdir -p '$remoteDir'")
    Invoke-Adb -Arguments @("push", $playerPath, $remotePlayer)
    Invoke-Adb -Arguments @("shell", "chmod 755 '$remotePlayer'")
    Invoke-Adb -Arguments @("shell", "$stopPlayerScript; cd /; nohup setsid '$remotePlayer' > '$remoteDir/hiby_player.log' 2>&1 < /dev/null & sleep 2")

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $localState) | Out-Null
    Set-Content -Path $localState -Value $remoteDir -Encoding ASCII

    Invoke-Adb -Arguments @("shell", "ps | grep hiby_player")
    Write-Host "Patched player started from $remotePlayer"
    Write-Host "Use -RestoreStock or reboot the device to return to stock runtime."
}

if ($RestoreStock) {
    Invoke-Adb -Arguments @("shell", "$stopPlayerScript; cd /; nohup setsid /usr/bin/hiby_player > /usr/data/codex_audiobook_stock_restart.log 2>&1 < /dev/null & sleep 2")
    Invoke-Adb -Arguments @("shell", "ps | grep hiby_player")
    Write-Host "Stock hiby_player.sh restart requested."
}
