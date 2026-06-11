param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [string]$CheckScript = "tools\check_audiobook_release_state.py",

    [Parameter(Mandatory=$false)]
    [string]$OutDir = "work\installed-release-verification",

    [Parameter(Mandatory=$false)]
    [string]$ExpectedVersion = "1.6.4-audiobook",

    [Parameter(Mandatory=$false)]
    [int]$MinUsrDataFreeKb = 4096,

    [switch]$RequireDbMaintenance = $true,

    [switch]$RequirePlayModeGuard,

    [switch]$CaptureFramebuffer
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Invoke-AdbText([string]$Command) {
    $output = & $adbPath shell $Command
    if ($LASTEXITCODE -ne 0) {
        throw "adb shell failed: $Command"
    }
    return ($output -join "`n")
}

function Invoke-AdbPull([string]$Remote, [string]$Local) {
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "SilentlyContinue"
    try {
        & $adbPath pull $Remote $Local | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "adb pull failed: $Remote"
        }
    }
    finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
}

function Assert-Contains([string]$Text, [string]$Needle, [string]$Label) {
    if ($Text -notlike "*$Needle*") {
        throw "$Label did not contain expected text: $Needle"
    }
    Write-Host "OK   $Label contains $Needle"
}

$adbPath = Resolve-PathStrict $Adb
$checkScriptPath = Resolve-PathStrict $CheckScript
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$verifyDir = Join-Path (Resolve-Path -LiteralPath (Get-Location).Path).Path (Join-Path $OutDir $stamp)
New-Item -ItemType Directory -Force -Path $verifyDir | Out-Null

& $adbPath devices | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

$versionText = Invoke-AdbText "cat /etc/r1_audiobook_version 2>/dev/null"
Set-Content -LiteralPath (Join-Path $verifyDir "r1_audiobook_version.txt") -Value $versionText
Assert-Contains $versionText "version=$ExpectedVersion" "/etc/r1_audiobook_version"

$configText = Invoke-AdbText "cat /usr/resource/config.json 2>/dev/null"
Set-Content -LiteralPath (Join-Path $verifyDir "resource_config.json") -Value $configText
Assert-Contains $configText $ExpectedVersion "/usr/resource/config.json"

$daemonText = Invoke-AdbText "ps | grep '[r]1_audiobook_resume_daemon' 2>/dev/null || true"
Set-Content -LiteralPath (Join-Path $verifyDir "daemon.txt") -Value $daemonText
if ([string]::IsNullOrWhiteSpace($daemonText)) {
    throw "resume daemon is not running"
}
Write-Host "OK   resume daemon is running"

if ($RequirePlayModeGuard) {
    $runtimeDaemonScript = Invoke-AdbText "cat /usr/data/audiobooks/bin/r1_audiobook_resume_daemon.sh 2>/dev/null || cat /usr/bin/r1_audiobook_resume_daemon.sh 2>/dev/null"
    Set-Content -LiteralPath (Join-Path $verifyDir "runtime_resume_daemon.sh") -Value $runtimeDaemonScript
    Assert-Contains $runtimeDaemonScript 'PLAY_MODE_TARGET=${AUDIOBOOK_PLAY_MODE_TARGET:-3}' "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "PLAY_MODE_USER_INI_OFFSET=" "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "ensure_audiobook_play_mode" "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "play-mode skipped screen-not-ready" "runtime resume daemon"

    $daemonLogTail = Invoke-AdbText "tail -80 /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true"
    Set-Content -LiteralPath (Join-Path $verifyDir "resume_daemon_log_tail.txt") -Value $daemonLogTail
    Assert-Contains $daemonLogTail "play_mode_target=3" "resume daemon log"

    $playModeByte = Invoke-AdbText "dd if=/usr/data/user.ini bs=1 skip=592 count=1 2>/dev/null | od -An -tu1 2>/dev/null || true"
    Set-Content -LiteralPath (Join-Path $verifyDir "play_mode_byte.txt") -Value $playModeByte
    Write-Host "OK   captured current play-mode byte: $($playModeByte.Trim())"
}

if ($RequireDbMaintenance) {
    $dbMaintText = Invoke-AdbText "ps | grep '[r]1_audiobook_db_watch' 2>/dev/null || true"
    Set-Content -LiteralPath (Join-Path $verifyDir "db_maint_daemon.txt") -Value $dbMaintText
    if ([string]::IsNullOrWhiteSpace($dbMaintText)) {
        throw "audiobook DB maintenance watcher is not running"
    }
    Write-Host "OK   audiobook DB maintenance watcher is running"

    $dbMaintFiles = Invoke-AdbText "ls -l /usr/data/audiobooks/bin/r1_audiobook_db_maint /usr/data/audiobooks/bin/r1_audiobook_db_watch.sh /usr/bin/r1_audiobook_db_maint /usr/bin/r1_audiobook_db_watch.sh 2>/dev/null"
    Set-Content -LiteralPath (Join-Path $verifyDir "db_maint_files.txt") -Value $dbMaintFiles
    Assert-Contains $dbMaintFiles "r1_audiobook_db_maint" "DB maintenance helper files"
    Assert-Contains $dbMaintFiles "r1_audiobook_db_watch.sh" "DB maintenance watcher files"
}

$uptText = Invoke-AdbText "if [ -e /usr/data/mnt/sd_0/r1.upt ]; then ls -l /usr/data/mnt/sd_0/r1.upt; else echo no-r1.upt; fi"
Set-Content -LiteralPath (Join-Path $verifyDir "r1_upt_status.txt") -Value $uptText
Assert-Contains $uptText "no-r1.upt" "SD update trigger status"

$userIniRefs = Invoke-AdbText "grep -a -i 'Audiobook\|Engulfed\|Squirrel\|Ice Like Fire' /usr/data/user.ini 2>/dev/null || true"
Set-Content -LiteralPath (Join-Path $verifyDir "user_ini_audiobook_refs.txt") -Value $userIniRefs
if ([string]::IsNullOrWhiteSpace($userIniRefs)) {
    Write-Host "OK   user.ini has no saved-last audiobook references"
}
else {
    Write-Host "WARN user.ini contains audiobook-related text; review $verifyDir\user_ini_audiobook_refs.txt"
}

$dfText = Invoke-AdbText "df -k /usr/data /usr/data/mnt/sd_0 2>/dev/null"
Set-Content -LiteralPath (Join-Path $verifyDir "df.txt") -Value $dfText
$usrDataLine = ($dfText -split "`n" | Where-Object { $_ -match "\s/usr/data$" } | Select-Object -First 1)
if ($usrDataLine -match "^\S+\s+\d+\s+\d+\s+(\d+)\s+") {
    $freeKb = [int]$Matches[1]
    if ($freeKb -lt $MinUsrDataFreeKb) {
        throw "/usr/data free space is below threshold: ${freeKb}KB < ${MinUsrDataFreeKb}KB"
    }
    Write-Host "OK   /usr/data free space ${freeKb}KB"
}
else {
    Write-Host "WARN could not parse /usr/data free space"
}

$dbLocal = Join-Path $verifyDir "usrlocal_media.db"
$catalogLocal = Join-Path $verifyDir "catalog.tsv"
$booksCatalogLocal = Join-Path $verifyDir "catalog-books.tsv"
Invoke-AdbPull "/usr/data/usrlocal_media.db" $dbLocal
Invoke-AdbPull "/usr/data/audiobooks/catalog.tsv" $catalogLocal
$booksCatalogArg = @()
$booksCatalogPresence = Invoke-AdbText "if [ -s /usr/data/audiobooks/catalog-books.tsv ]; then echo present; else echo missing; fi"
if ($booksCatalogPresence -match "present") {
    Invoke-AdbPull "/usr/data/audiobooks/catalog-books.tsv" $booksCatalogLocal
    $booksCatalogArg = @("--books-catalog", $booksCatalogLocal)
}

python $checkScriptPath $dbLocal --catalog $catalogLocal @booksCatalogArg --expect-audiobooks
if ($LASTEXITCODE -ne 0) {
    throw "release-state database check failed"
}

$hashLines = @()
foreach ($path in @($dbLocal, $catalogLocal, $booksCatalogLocal)) {
    if (!(Test-Path -LiteralPath $path)) {
        continue
    }
    $hash = (Get-FileHash -Algorithm MD5 -LiteralPath $path).Hash.ToLowerInvariant()
    $item = Get-Item -LiteralPath $path
    $hashLines += "$hash  $($item.Length)  $path"
}
Set-Content -LiteralPath (Join-Path $verifyDir "hashes.txt") -Value $hashLines

$devArtifactCommand = @'
for n in debug-daemon.out debug-daemon.pid dmr-probe.out helper-current.out helper-current.strace mem-pos-near.bin player-restart.out player-restart2.out position-watch-holidays-on-ice-2008.nohup.log position-watch-holidays-on-ice-2008.pid position-watch-holidays.loop.log position-watch-holidays.nohup.log position-watch-holidays.pid ptrwins r1_audiobook_resume_daemon.syntax-test.sh resume-daemon.testpid resume-daemon.trace scan_skip_runtime_patch.json tracklist-window.bin user.ini.before-stock-audiobook-last-clear; do
  [ -e "/usr/data/audiobooks/$n" ] && echo "$n"
done
'@
$devArtifactCheck = Invoke-AdbText $devArtifactCommand
Set-Content -LiteralPath (Join-Path $verifyDir "dev_artifacts_remaining.txt") -Value $devArtifactCheck
if ([string]::IsNullOrWhiteSpace($devArtifactCheck)) {
    Write-Host "OK   no known development artifacts remain in /usr/data/audiobooks"
}
else {
    Write-Host "WARN known development artifacts remain; review $verifyDir\dev_artifacts_remaining.txt"
}

if ($CaptureFramebuffer) {
    $fbPng = Join-Path $verifyDir "fb0.png"
    python "tools\adb_capture_fb0.py" --adb $adbPath --output $fbPng
    if ($LASTEXITCODE -ne 0) {
        throw "framebuffer capture failed"
    }
}

Write-Host ""
Write-Host "Installed audiobook release verification passed."
Write-Host "Artifacts: $verifyDir"
