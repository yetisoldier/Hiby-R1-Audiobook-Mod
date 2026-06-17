param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "",

    [Parameter(Mandatory=$false)]
    [string]$CheckScript = "tools\check_audiobook_release_state.py",

    [Parameter(Mandatory=$false)]
    [string]$OutDir = "work\installed-release-verification",

    [Parameter(Mandatory=$false)]
    [string]$ExpectedVersion = "1.6.16.2-audiobook",

    [Parameter(Mandatory=$false)]
    [int]$MinUsrDataFreeKb = 4096,

    [switch]$RequireDbMaintenance = $true,

    [switch]$RequirePlayModeGuard,

    [switch]$RequireDbBootStabilityGuard,

    [switch]$RequireContextStartGuard,

    [switch]$ExpectNativeDsd,

    [switch]$ExpectBluetoothSbcXq,

    [switch]$ExpectUsbDacMode,

    [switch]$AllowStagedFirmware,

    [switch]$CaptureFramebuffer
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
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

$adbPath = Resolve-AdbPath $Adb
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

if ($ExpectNativeDsd) {
    Assert-Contains $versionText "native_dsd=enabled" "/etc/r1_audiobook_version"
}
if ($ExpectBluetoothSbcXq) {
    Assert-Contains $versionText "bluetooth_sbc_xq=enabled" "/etc/r1_audiobook_version"
}
if ($ExpectUsbDacMode) {
    Assert-Contains $versionText "usb_dac_mode=enabled" "/etc/r1_audiobook_version"
}

$configText = Invoke-AdbText "cat /usr/resource/config.json 2>/dev/null"
Set-Content -LiteralPath (Join-Path $verifyDir "resource_config.json") -Value $configText
Assert-Contains $configText $ExpectedVersion "/usr/resource/config.json"

if ($ExpectUsbDacMode) {
    Assert-Contains $configText '"dac_to_store": 1' "/usr/resource/config.json"
    $setFunctionsText = Invoke-AdbText "cat /usr/resource/set_functions.json 2>/dev/null"
    Set-Content -LiteralPath (Join-Path $verifyDir "set_functions.json") -Value $setFunctionsText
    $midiSetFunctionsText = Invoke-AdbText "cat /usr/resource/midi_set_functions.json 2>/dev/null"
    Set-Content -LiteralPath (Join-Path $verifyDir "midi_set_functions.json") -Value $midiSetFunctionsText
    foreach ($flag in @("usb_mode", "dac_feedback", "car_mode", "standby", "about")) {
        Assert-Contains $setFunctionsText ('"' + $flag + '": 1') "/usr/resource/set_functions.json"
        Assert-Contains $midiSetFunctionsText ('"' + $flag + '": 1') "/usr/resource/midi_set_functions.json"
    }
}

if ($ExpectNativeDsd) {
    $otDevicesText = Invoke-AdbText "cat /usr/resource/ot_devices.json 2>/dev/null"
    Set-Content -LiteralPath (Join-Path $verifyDir "ot_devices.json") -Value $otDevicesText
    Assert-Contains $otDevicesText '"AnalogDsdNative": "native"' "/usr/resource/ot_devices.json"
    Assert-Contains $otDevicesText '"AnalogDsdD2p": "dop"' "/usr/resource/ot_devices.json"
    Assert-Contains $otDevicesText '"AnalogDsdDop": "dop"' "/usr/resource/ot_devices.json"
}

if ($ExpectBluetoothSbcXq) {
    $btInitText = Invoke-AdbText "cat /usr/bin/bt_init 2>/dev/null"
    Set-Content -LiteralPath (Join-Path $verifyDir "bt_init") -Value $btInitText
    if ($btInitText -like "*`r*") {
        throw "/usr/bin/bt_init contains CR characters; BusyBox sh may fail to parse it"
    }
    Write-Host "OK   /usr/bin/bt_init uses LF line endings"
    Assert-Contains $btInitText '/usr/bin/bluealsa -p a2dp-source --a2dp-volume --sbc-quality=xq &' "/usr/bin/bt_init"
}

$daemonText = Invoke-AdbText "ps | grep '[r]1_audiobook_resume_daemon' 2>/dev/null || true"
Set-Content -LiteralPath (Join-Path $verifyDir "daemon.txt") -Value $daemonText
if ([string]::IsNullOrWhiteSpace($daemonText)) {
    throw "resume daemon is not running"
}
Write-Host "OK   resume daemon is running"

$runtimeDaemonScript = Invoke-AdbText "cat /usr/data/audiobooks/bin/r1_audiobook_resume_daemon.sh 2>/dev/null || cat /usr/bin/r1_audiobook_resume_daemon.sh 2>/dev/null"
Set-Content -LiteralPath (Join-Path $verifyDir "runtime_resume_daemon.sh") -Value $runtimeDaemonScript
Assert-Contains $runtimeDaemonScript 'LOG_MAX_BYTES=${AUDIOBOOK_RESUME_LOG_MAX_BYTES:-524288}' "runtime resume daemon"
Assert-Contains $runtimeDaemonScript "rotate_log_if_needed" "runtime resume daemon"
Assert-Contains $runtimeDaemonScript 'SAVE_BUCKET_MS=${AUDIOBOOK_SAVE_BUCKET_MS:-15000}' "runtime resume daemon"
Assert-Contains $runtimeDaemonScript 'bucket=$((pos / SAVE_BUCKET_MS))' "runtime resume daemon"

if ($RequirePlayModeGuard) {
    Assert-Contains $runtimeDaemonScript 'PLAY_MODE_TARGET=${AUDIOBOOK_PLAY_MODE_TARGET:-3}' "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "PLAY_MODE_USER_INI_OFFSET=" "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "ensure_audiobook_play_mode" "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "play-mode skipped screen-not-ready" "runtime resume daemon"

    $daemonLogPlayMode = Invoke-AdbText "grep -a 'play_mode_target=3' /usr/data/audiobooks/resume-daemon.log 2>/dev/null | tail -5 || true"
    Set-Content -LiteralPath (Join-Path $verifyDir "resume_daemon_play_mode_log.txt") -Value $daemonLogPlayMode
    Assert-Contains $daemonLogPlayMode "play_mode_target=3" "resume daemon log"

    $playModeByte = Invoke-AdbText "dd if=/usr/data/user.ini bs=1 skip=592 count=1 2>/dev/null | od -An -tu1 2>/dev/null || true"
    Set-Content -LiteralPath (Join-Path $verifyDir "play_mode_byte.txt") -Value $playModeByte
    Write-Host "OK   captured current play-mode byte: $($playModeByte.Trim())"
}

if ($RequireContextStartGuard) {
    Assert-Contains $runtimeDaemonScript "allow_memscan_root" "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "book_title_should_preplay_direct_start" "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "book_title_preplay_allow_memscan_root" "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript 'launcher|context|path|relaxed) printf' "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "book-title touch-first skipped reason=launcher" "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript 'restored_path:-' "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "autostart_restore_active" "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "book-title direct-start skipped reason=" "runtime resume daemon"
    Assert-Contains $runtimeDaemonScript "restore settle after track restore path=" "runtime resume daemon"
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

    $dbWatchScript = Invoke-AdbText "cat /usr/data/audiobooks/bin/r1_audiobook_db_watch.sh 2>/dev/null || cat /usr/bin/r1_audiobook_db_watch.sh 2>/dev/null"
    Set-Content -LiteralPath (Join-Path $verifyDir "runtime_db_watch.sh") -Value $dbWatchScript
    Assert-Contains $dbWatchScript 'LOG_MAX_BYTES=${AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES:-524288}' "runtime DB watcher"
    Assert-Contains $dbWatchScript "rotate_log_if_needed" "runtime DB watcher"
    Assert-Contains $dbWatchScript 'LOCK_DIR=${AUDIOBOOK_DB_MAINT_LOCK:-$BASE/db-maint.lock}' "runtime DB watcher"
    Assert-Contains $dbWatchScript "pid_is_db_watcher" "runtime DB watcher"
    Assert-Contains $dbWatchScript "stale-lock-live-pid-not-watcher" "runtime DB watcher"
    Assert-Contains $dbWatchScript "trap 'cleanup; exit 0' HUP INT TERM" "runtime DB watcher"
    Assert-Contains $dbWatchScript "exit reason=already-running" "runtime DB watcher"
    Assert-Contains $dbWatchScript 'compare_last_size=$(signature_size "$last_sig")' "runtime DB watcher"

    if ($RequireDbBootStabilityGuard) {
        Assert-Contains $dbWatchScript 'BOOT_STABLE_TIMEOUT_SECONDS=${AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS:-180}' "runtime DB watcher"
        Assert-Contains $dbWatchScript 'STABLE_POLL_SECONDS=${AUDIOBOOK_DB_STABLE_POLL_SECONDS:-3}' "runtime DB watcher"
        Assert-Contains $dbWatchScript "wait_for_stable_db boot" "runtime DB watcher"
        Assert-Contains $dbWatchScript "wait-stable-timeout reason=" "runtime DB watcher"
        Assert-Contains $dbWatchScript "AUDIOBOOK_DB_MIRROR_PATHS=" "runtime DB watcher"
        Assert-Contains $dbWatchScript "/data/usrlocal_media.db" "runtime DB watcher"
        Assert-Contains $dbWatchScript '$SD_ROOT/usrlocal_media.db' "runtime DB watcher"
        Assert-Contains $dbWatchScript "run_maint_one_db" "runtime DB watcher"
        Assert-Contains $dbWatchScript 'run_maint_one_db "$reason" "$mirror_db" mirror' "runtime DB watcher"
        Assert-Contains $dbWatchScript "copy_primary_to_mirror" "runtime DB watcher"
        Assert-Contains $dbWatchScript "mirror-copy reason=" "runtime DB watcher"
        Assert-Contains $dbWatchScript "any_db_needs_maintenance" "runtime DB watcher"
        Assert-Contains $dbWatchScript "--needs-maintenance" "runtime DB watcher"
        Assert-Contains $dbWatchScript "content-repair-mtime" "runtime DB watcher"
        Assert-Contains $dbWatchScript "boot_stable_timeout=" "runtime DB watcher"
        Assert-Contains $dbWatchScript "zero_audio_retry=" "runtime DB watcher"
        Assert-Contains $dbWatchScript "retry_zero_audiobooks_if_needed boot" "runtime DB watcher"
        Assert-Contains $dbWatchScript "zero-audiobook-retry-ready" "runtime DB watcher"
        Assert-Contains $dbWatchScript 'audiobook_tracks=${LAST_AUDIOBOOK_TRACKS:-unknown}' "runtime DB watcher"

        $dbInitScript = Invoke-AdbText "cat /etc/init.d/S92audiobook_db_maint.sh 2>/dev/null"
        Set-Content -LiteralPath (Join-Path $verifyDir "runtime_db_init.sh") -Value $dbInitScript
        Assert-Contains $dbInitScript "AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS=180" "runtime DB init"
        Assert-Contains $dbInitScript "AUDIOBOOK_DB_STABLE_POLL_SECONDS=3" "runtime DB init"
        Assert-Contains $dbInitScript "AUDIOBOOK_DB_ZERO_AUDIO_RETRY_TIMEOUT_SECONDS=180" "runtime DB init"
        Assert-Contains $dbInitScript "AUDIOBOOK_DB_ZERO_AUDIO_RETRY_POLL_SECONDS=5" "runtime DB init"
        Assert-Contains $dbInitScript "db_watch_pid_is_live()" "runtime DB init"
        Assert-Contains $dbInitScript "stop_db_watch()" "runtime DB init"
        Assert-Contains $dbInitScript 'kill -9 "$old_pid"' "runtime DB init"
        Assert-Contains $dbInitScript 'rm -rf "$BASE/db-maint.lock"' "runtime DB init"
    }
}

$uptText = Invoke-AdbText "if [ -e /usr/data/mnt/sd_0/r1.upt ]; then ls -l /usr/data/mnt/sd_0/r1.upt; else echo no-r1.upt; fi"
Set-Content -LiteralPath (Join-Path $verifyDir "r1_upt_status.txt") -Value $uptText
if ($AllowStagedFirmware) {
    if ($uptText -like "*no-r1.upt*") {
        Write-Host "OK   no staged firmware package is present"
    }
    else {
        Write-Host "OK   staged firmware package is present by request"
    }
}
else {
    Assert-Contains $uptText "no-r1.upt" "SD update trigger status"
}

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
$titlesCatalogLocal = Join-Path $verifyDir "catalog-view-title.tsv"
$authorsCatalogLocal = Join-Path $verifyDir "catalog-view-author.tsv"
$seriesCatalogLocal = Join-Path $verifyDir "catalog-view-series.tsv"
Invoke-AdbPull "/usr/data/usrlocal_media.db" $dbLocal
Invoke-AdbPull "/usr/data/audiobooks/catalog.tsv" $catalogLocal
$booksCatalogArg = @()
$viewCatalogArgs = @()
$booksCatalogPresence = Invoke-AdbText "if [ -s /usr/data/audiobooks/catalog-books.tsv ]; then echo present; else echo missing; fi"
if ($booksCatalogPresence -match "present") {
    Invoke-AdbPull "/usr/data/audiobooks/catalog-books.tsv" $booksCatalogLocal
    $booksCatalogArg = @("--books-catalog", $booksCatalogLocal)
}
$titlesCatalogPresence = Invoke-AdbText "if [ -s /usr/data/audiobooks/catalog-view-title.tsv ]; then echo present; else echo missing; fi"
if ($titlesCatalogPresence -match "present") {
    Invoke-AdbPull "/usr/data/audiobooks/catalog-view-title.tsv" $titlesCatalogLocal
    $viewCatalogArgs += @("--titles-catalog", $titlesCatalogLocal)
}
$authorsCatalogPresence = Invoke-AdbText "if [ -s /usr/data/audiobooks/catalog-view-author.tsv ]; then echo present; else echo missing; fi"
if ($authorsCatalogPresence -match "present") {
    Invoke-AdbPull "/usr/data/audiobooks/catalog-view-author.tsv" $authorsCatalogLocal
    $viewCatalogArgs += @("--authors-catalog", $authorsCatalogLocal)
}
$seriesCatalogPresence = Invoke-AdbText "if [ -s /usr/data/audiobooks/catalog-view-series.tsv ]; then echo present; else echo missing; fi"
if ($seriesCatalogPresence -match "present") {
    Invoke-AdbPull "/usr/data/audiobooks/catalog-view-series.tsv" $seriesCatalogLocal
    $viewCatalogArgs += @("--series-catalog", $seriesCatalogLocal)
}

python $checkScriptPath $dbLocal --catalog $catalogLocal @booksCatalogArg @viewCatalogArgs --expect-audiobooks
if ($LASTEXITCODE -ne 0) {
    throw "release-state database check failed"
}

$hashLines = @()
foreach ($path in @($dbLocal, $catalogLocal, $booksCatalogLocal, $titlesCatalogLocal, $authorsCatalogLocal, $seriesCatalogLocal)) {
    if (!(Test-Path -LiteralPath $path)) {
        continue
    }
    $hash = (Get-FileHash -Algorithm MD5 -LiteralPath $path).Hash.ToLowerInvariant()
    $item = Get-Item -LiteralPath $path
    $hashLines += "$hash  $($item.Length)  $path"
}
Set-Content -LiteralPath (Join-Path $verifyDir "hashes.txt") -Value $hashLines

$devArtifactCommand = @'
for n in debug-daemon.out debug-daemon.pid dmr-probe.out helper-current.out helper-current.strace mem-pos-near.bin player-restart.out player-restart2.out position-watch-holidays-on-ice-2008.nohup.log position-watch-holidays-on-ice-2008.pid position-watch-holidays.loop.log position-watch-holidays.nohup.log position-watch-holidays.pid ptrwins r1_audiobook_resume_daemon.syntax-test.sh resume-daemon.testpid resume-daemon.trace scan_skip_runtime_patch.json tracklist-window.bin; do
  [ -e "/usr/data/audiobooks/$n" ] && echo "$n"
done
for n in r1_audiobook_position_watch.sh r1_audiobook_resume.sh r1_audiobook_resume_daemon.sh.syntaxcheck r1_dmr_probe_helper r1_unix_socket_write r1_utf16_root_scan_probe.sh; do
  [ -e "/usr/data/audiobooks/bin/$n" ] && echo "bin/$n"
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
