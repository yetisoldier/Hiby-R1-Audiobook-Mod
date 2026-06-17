param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "",

    [Parameter(Mandatory=$false)]
    [switch]$Start,

    [Parameter(Mandatory=$false)]
    [switch]$Stop,

    [Parameter(Mandatory=$false)]
    [switch]$ForceRestart
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$marker = Join-Path $repoRoot "work\adb-control\held-hiby-player-adb.pid"

function Resolve-AdbPath {
    param([string]$Requested)
    if ($Requested) {
        if (Test-Path -LiteralPath $Requested) {
            return (Resolve-Path -LiteralPath $Requested).Path
        }
        $found = Get-Command $Requested -ErrorAction SilentlyContinue
        if ($found) {
            return $found.Source
        }
    }
    $repoAdb = Join-Path $repoRoot ".tools\platform-tools\adb.exe"
    if (Test-Path -LiteralPath $repoAdb) {
        return (Resolve-Path -LiteralPath $repoAdb).Path
    }
    $installed = "C:\Program Files\Software Fix\adb.exe"
    if (Test-Path -LiteralPath $installed) {
        return $installed
    }
    $pathAdb = Get-Command adb.exe -ErrorAction SilentlyContinue
    if ($pathAdb) {
        return $pathAdb.Source
    }
    throw "ADB not found"
}

function Invoke-AdbShell {
    param(
        [string]$AdbPath,
        [string]$Command
    )
    & $AdbPath shell $Command
    if ($LASTEXITCODE -ne 0) {
        throw "adb shell failed: $Command"
    }
}

function Get-RemotePlayerPids {
    param([string]$AdbPath)
    $out = & $AdbPath shell "ps | sed -n '/\/usr\/bin\/hiby_player$/ { /grep/d; s/^ *\([0-9][0-9]*\).*/\1/p; }'"
    if ($LASTEXITCODE -ne 0) {
        throw "failed to query hiby_player"
    }
    return @($out -split "`r?`n" | Where-Object { $_ -match '^\d+$' })
}

function Stop-HeldAdbProcess {
    $stopped = $false
    if (Test-Path -LiteralPath $marker) {
        $pidText = (Get-Content -LiteralPath $marker -Raw).Trim()
        if ($pidText -match '^\d+$') {
            $proc = Get-Process -Id ([int]$pidText) -ErrorAction SilentlyContinue
            if ($proc) {
                Stop-Process -Id $proc.Id -Force
                Write-Host "stopped held adb process pid=$($proc.Id)"
                $stopped = $true
            }
        }
        Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
    }

    $held = Get-CimInstance Win32_Process -Filter "name = 'adb.exe'" |
        Where-Object { $_.CommandLine -match 'shell\s+/usr/bin/hiby_player' }
    foreach ($proc in $held) {
        Stop-Process -Id $proc.ProcessId -Force
        Write-Host "stopped matching adb holder pid=$($proc.ProcessId)"
        $stopped = $true
    }

    if (-not $stopped) {
        Write-Host "no held adb player process found"
    }
}

if (-not $Start -and -not $Stop) {
    $Start = $true
}
if ($Start -and $Stop) {
    throw "choose only one of -Start or -Stop"
}

$adbPath = Resolve-AdbPath $Adb
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $marker) | Out-Null

if ($Stop) {
    Stop-HeldAdbProcess
    exit 0
}

$existing = Get-RemotePlayerPids $adbPath
if ($existing.Count -gt 0 -and -not $ForceRestart) {
    Write-Host "hiby_player already running: $($existing -join ', ')"
    Write-Host "use -ForceRestart only when the UI is wedged and you intentionally want to stop the player"
    exit 0
}

if ($ForceRestart) {
    Stop-HeldAdbProcess
    Invoke-AdbShell $adbPath "for p in `$(ps | sed -n '/hiby_player.sh/ { /sed/d; s/^ *\([0-9][0-9]*\).*/\1/p; }'); do kill `$p 2>/dev/null; done; sleep 0.2; killall hiby_player 2>/dev/null || true"
    Start-Sleep -Milliseconds 500
}

$proc = Start-Process -FilePath $adbPath -ArgumentList @("shell", "/usr/bin/hiby_player") -WindowStyle Hidden -PassThru
Set-Content -LiteralPath $marker -Value $proc.Id -Encoding ASCII
Write-Host "started hidden adb holder pid=$($proc.Id)"
Start-Sleep -Seconds 2

$running = Get-RemotePlayerPids $adbPath
if ($running.Count -eq 0) {
    throw "hiby_player did not stay running"
}
Write-Host "hiby_player running: $($running -join ', ')"
