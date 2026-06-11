param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [string]$HelperSource = "work\device-audiobook-helper-20260609\audiobooks\bin\r1_audiobook_resume_helper",

    [Parameter(Mandatory=$false)]
    [string]$MemscanHelperSource = "work\native-memscan\r1_audiobook_memscan",

    [Parameter(Mandatory=$false)]
    [string]$DaemonSource = "tools\r1_audiobook_resume_daemon.sh",

    [Parameter(Mandatory=$false)]
    [string]$CatalogSource = "",

    [Parameter(Mandatory=$false)]
    [switch]$RestoreEnabled,

    [Parameter(Mandatory=$false)]
    [switch]$DisableTrackRestore,

    [Parameter(Mandatory=$false)]
    [switch]$DisableBookTitlePathGuard,

    [Parameter(Mandatory=$false)]
    [switch]$DisableBookTitleDirectTrackSelect,

    [Parameter(Mandatory=$false)]
    [switch]$DisableBookTitleDirectTrackPreplay,

    [Parameter(Mandatory=$false)]
    [switch]$DisableUiSeekFallback,

    [Parameter(Mandatory=$false)]
    [switch]$DisableUiSeekScreenGuard,

    [Parameter(Mandatory=$false)]
    [string]$TouchNextEventSource = "",

    [Parameter(Mandatory=$false)]
    [string]$TouchFirstTrackEventSource = "",

    [Parameter(Mandatory=$false)]
    [string]$KeyNextEventSource = "",

    [Parameter(Mandatory=$false)]
    [string]$KeyPrevEventSource = "",

    [Parameter(Mandatory=$false)]
    [ValidateSet("memory", "helper")]
    [string]$PositionSource = "memory",

    [Parameter(Mandatory=$false)]
    [int]$IntervalSeconds = 1,

    [Parameter(Mandatory=$false)]
    [int]$IdleIntervalSeconds = 5,

    [Parameter(Mandatory=$false)]
    [int]$BookTitleMarkerIdlePollSeconds = 5,

    [Parameter(Mandatory=$false)]
    [int]$BookTitleMarkerMusicPollSeconds = 15,

    [Parameter(Mandatory=$false)]
    [int]$DiagnosticsIntervalSeconds = 60,

    [Parameter(Mandatory=$false)]
    [int]$BookTitleAutostartDelaySeconds = 1,

    [Parameter(Mandatory=$false)]
    [int]$BookTitleContextSeconds = 300,

    [Parameter(Mandatory=$false)]
    [int]$NewTrackCommitMs = 15000,

    [Parameter(Mandatory=$false)]
    [int]$RestoreRetryMaxAfterFailureSeconds = 300,

    [Parameter(Mandatory=$false)]
    [int]$FailedRestoreSkipLogBucketMs = 30000,

    [Parameter(Mandatory=$false)]
    [int]$BookTitleRestoreLogBucketMs = 5000,

    [Parameter(Mandatory=$false)]
    [int]$ResumeLogMaxBytes = 524288,

    [Parameter(Mandatory=$false)]
    [int]$BookTitleDirectTrackMaxSwipes = 20,

    [Parameter(Mandatory=$false)]
    [ValidateRange(1, 5)]
    [int]$BookTitleDirectTrackVisibleRows = 5,

    [Parameter(Mandatory=$false)]
    [int]$BookTitleDirectTrackRowsPerSwipe = 4,

    [Parameter(Mandatory=$false)]
    [switch]$DisableBookTitleMemscan,

    [Parameter(Mandatory=$false)]
    [switch]$DisableBookTitleDirectTrackCalibrate,

    [Parameter(Mandatory=$false)]
    [switch]$DisableBookTitleDirectTrackRecovery,

    [Parameter(Mandatory=$false)]
    [int]$BookTitleDirectTrackRecoveryMaxSteps = 20,

    [Parameter(Mandatory=$false)]
    [int]$UiSeekBarXMin = 21,

    [Parameter(Mandatory=$false)]
    [int]$UiSeekBarXMax = 459,

    [Parameter(Mandatory=$false)]
    [int]$UiSeekBarY = 619,

    [Parameter(Mandatory=$false)]
    [int]$UiSeekVerifyToleranceMs = 15000,

    [Parameter(Mandatory=$false)]
    [int]$UiSeekTouchFrames = 2,

    [Parameter(Mandatory=$false)]
    [int]$UiSeekScreenMinBarPixels = 300,

    [Parameter(Mandatory=$false)]
    [string]$RemoteBase = "/usr/data/audiobooks"
)

$ErrorActionPreference = "Stop"

function Require-Path([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

$adbPath = Require-Path $Adb
$helperPath = Require-Path $HelperSource
$memscanHelperPath = Require-Path $MemscanHelperSource
$daemonPath = Require-Path $DaemonSource
$catalogPath = $null
if ($CatalogSource) {
    $catalogPath = Require-Path $CatalogSource
}
$touchNextEventPath = $null
if ($TouchNextEventSource) {
    $touchNextEventPath = Require-Path $TouchNextEventSource
} else {
    $generatedTouchEvent = "work\r1-touch-events\touch_next_event1.bin"
    python tools\adb_inject_touch_event.py --output $generatedTouchEvent | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate touch-next event stream"
    }
    $touchNextEventPath = Require-Path $generatedTouchEvent
}
$touchFirstTrackEventPath = $null
$touchFirstTrackDownEventPath = $null
$touchFirstTrackMoveEventPath = $null
$touchFirstTrackUpEventPath = $null
if ($TouchFirstTrackEventSource) {
    $touchFirstTrackEventPath = Require-Path $TouchFirstTrackEventSource
} else {
    $generatedTouchFirstTrackEvent = "work\r1-touch-events\touch_first_track_event1.bin"
    python tools\adb_inject_touch_event.py --x 203 --y 197 --output $generatedTouchFirstTrackEvent | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate first-track touch event stream"
    }
    $touchFirstTrackEventPath = Require-Path $generatedTouchFirstTrackEvent
}
$generatedTouchFirstTrackDownEvent = "work\r1-touch-events\touch_first_track_down_event1.bin"
python tools\adb_inject_touch_event.py --x 203 --y 197 --touch-phase down --output $generatedTouchFirstTrackDownEvent | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "failed to generate first-track down event stream"
}
$touchFirstTrackDownEventPath = Require-Path $generatedTouchFirstTrackDownEvent
$generatedTouchFirstTrackMoveEvent = "work\r1-touch-events\touch_first_track_move_event1.bin"
python tools\adb_inject_touch_event.py --x 203 --y 197 --touch-phase move --output $generatedTouchFirstTrackMoveEvent | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "failed to generate first-track move event stream"
}
$touchFirstTrackMoveEventPath = Require-Path $generatedTouchFirstTrackMoveEvent
$generatedTouchFirstTrackUpEvent = "work\r1-touch-events\touch_first_track_up_event1.bin"
python tools\adb_inject_touch_event.py --x 203 --y 197 --touch-phase up --output $generatedTouchFirstTrackUpEvent | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "failed to generate first-track up event stream"
}
$touchFirstTrackUpEventPath = Require-Path $generatedTouchFirstTrackUpEvent
$generatedTouchBackEvent = "work\r1-touch-events\touch_back_event1.bin"
python tools\adb_inject_touch_event.py --x 30 --y 400 --to-x 360 --to-y 400 --drag-frames 18 --output $generatedTouchBackEvent | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "failed to generate back touch event stream"
}
$touchBackEventPath = Require-Path $generatedTouchBackEvent
$touchTrackRow1EventPath = $touchFirstTrackEventPath
$trackRowSpecs = @(
    @{ Name = "touch_track_row2_event1.bin"; X = 203; Y = 325 },
    @{ Name = "touch_track_row3_event1.bin"; X = 203; Y = 453 },
    @{ Name = "touch_track_row4_event1.bin"; X = 203; Y = 581 },
    @{ Name = "touch_track_row5_event1.bin"; X = 203; Y = 745 }
)
$touchTrackRowEventPaths = @($touchTrackRow1EventPath)
foreach ($spec in $trackRowSpecs) {
    $generatedTrackRowEvent = "work\r1-touch-events\$($spec.Name)"
    python tools\adb_inject_touch_event.py --x $spec.X --y $spec.Y --output $generatedTrackRowEvent | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate track-row touch event stream $($spec.Name)"
    }
    $touchTrackRowEventPaths += (Require-Path $generatedTrackRowEvent)
}
$swipePhaseSpecs = @(
    @{ Name = "touch_track_swipe_down_event1.bin"; X = 120; Y = 680; Phase = "down" },
    @{ Name = "touch_track_swipe_move1_event1.bin"; X = 120; Y = 600; Phase = "move" },
    @{ Name = "touch_track_swipe_move2_event1.bin"; X = 120; Y = 520; Phase = "move" },
    @{ Name = "touch_track_swipe_move3_event1.bin"; X = 120; Y = 440; Phase = "move" },
    @{ Name = "touch_track_swipe_move4_event1.bin"; X = 120; Y = 360; Phase = "move" },
    @{ Name = "touch_track_swipe_move5_event1.bin"; X = 120; Y = 280; Phase = "move" },
    @{ Name = "touch_track_swipe_move6_event1.bin"; X = 120; Y = 220; Phase = "move" },
    @{ Name = "touch_track_swipe_up_event1.bin"; X = 120; Y = 220; Phase = "up" }
)
$touchTrackSwipeEventPaths = @()
foreach ($spec in $swipePhaseSpecs) {
    $generatedSwipeEvent = "work\r1-touch-events\$($spec.Name)"
    python tools\adb_inject_touch_event.py --x $spec.X --y $spec.Y --touch-phase $spec.Phase --output $generatedSwipeEvent | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate track-swipe event stream $($spec.Name)"
    }
    $touchTrackSwipeEventPaths += (Require-Path $generatedSwipeEvent)
}
$keyNextEventPath = $null
if ($KeyNextEventSource) {
    $keyNextEventPath = Require-Path $KeyNextEventSource
} else {
    $generatedKeyNextEvent = "work\r1-touch-events\key_next_event0.bin"
    python tools\adb_inject_touch_event.py --button next --output $generatedKeyNextEvent | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate key-next event stream"
    }
    $keyNextEventPath = Require-Path $generatedKeyNextEvent
}
$keyPrevEventPath = $null
if ($KeyPrevEventSource) {
    $keyPrevEventPath = Require-Path $KeyPrevEventSource
} else {
    $generatedKeyPrevEvent = "work\r1-touch-events\key_prev_event2.bin"
    python tools\adb_inject_touch_event.py --button prev --output $generatedKeyPrevEvent | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate key-prev event stream"
    }
    $keyPrevEventPath = Require-Path $generatedKeyPrevEvent
}
$trackRestoreValue = if ($RestoreEnabled -and -not $DisableTrackRestore) { "1" } else { "0" }
$directTrackSelectValue = if ($DisableBookTitleDirectTrackSelect) { "0" } else { "1" }
$directTrackPreplayValue = if ($DisableBookTitleDirectTrackPreplay) { "0" } else { "1" }
$bookTitleMemscanValue = if ($DisableBookTitleMemscan) { "0" } else { "1" }
$bookTitleDirectTrackCalibrateValue = if ($DisableBookTitleDirectTrackCalibrate) { "0" } else { "1" }
$bookTitleDirectTrackRecoveryValue = if ($DisableBookTitleDirectTrackRecovery) { "0" } else { "1" }
$uiSeekFallbackValue = if ($DisableUiSeekFallback) { "0" } else { "1" }
$uiSeekScreenGuardValue = if ($DisableUiSeekScreenGuard) { "0" } else { "1" }

& $adbPath devices
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

& $adbPath shell "mkdir -p '$RemoteBase/bin' '$RemoteBase/input' '$RemoteBase/resume.d'"
if ($LASTEXITCODE -ne 0) {
    throw "failed to create remote runtime directories"
}

& $adbPath push $helperPath "$RemoteBase/bin/r1_audiobook_resume_helper"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push resume helper"
}

& $adbPath push $memscanHelperPath "$RemoteBase/bin/r1_audiobook_memscan"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push memscan helper"
}

& $adbPath push $daemonPath "$RemoteBase/bin/r1_audiobook_resume_daemon.sh"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push resume daemon"
}

if ($catalogPath) {
    & $adbPath push $catalogPath "$RemoteBase/catalog.tsv"
    if ($LASTEXITCODE -ne 0) {
        throw "failed to push resume catalog"
    }
}

& $adbPath push $touchNextEventPath "$RemoteBase/input/touch_next_event1.bin"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push touch-next event stream"
}

& $adbPath push $touchFirstTrackEventPath "$RemoteBase/input/touch_first_track_event1.bin"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push first-track touch event stream"
}
& $adbPath push $touchFirstTrackDownEventPath "$RemoteBase/input/touch_first_track_down_event1.bin"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push first-track down event stream"
}
& $adbPath push $touchFirstTrackMoveEventPath "$RemoteBase/input/touch_first_track_move_event1.bin"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push first-track move event stream"
}
& $adbPath push $touchFirstTrackUpEventPath "$RemoteBase/input/touch_first_track_up_event1.bin"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push first-track up event stream"
}
& $adbPath push $touchBackEventPath "$RemoteBase/input/touch_back_event1.bin"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push back touch event stream"
}
for ($i = 0; $i -lt $touchTrackRowEventPaths.Count; $i++) {
    $rowNumber = $i + 1
    & $adbPath push $touchTrackRowEventPaths[$i] "$RemoteBase/input/touch_track_row${rowNumber}_event1.bin"
    if ($LASTEXITCODE -ne 0) {
        throw "failed to push track-row $rowNumber touch event stream"
    }
}
foreach ($path in $touchTrackSwipeEventPaths) {
    $leafName = Split-Path -Leaf $path
    & $adbPath push $path "$RemoteBase/input/$leafName"
    if ($LASTEXITCODE -ne 0) {
        throw "failed to push track-swipe event stream $leafName"
    }
}

& $adbPath push $keyNextEventPath "$RemoteBase/input/key_next_event0.bin"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push key-next event stream"
}

& $adbPath push $keyPrevEventPath "$RemoteBase/input/key_prev_event2.bin"
if ($LASTEXITCODE -ne 0) {
    throw "failed to push key-prev event stream"
}

$installCommand = @"
chmod 755 '$RemoteBase/bin/r1_audiobook_resume_helper' '$RemoteBase/bin/r1_audiobook_memscan' '$RemoteBase/bin/r1_audiobook_resume_daemon.sh';
start-stop-daemon -K -p '$RemoteBase/resume-daemon.ssd.pid' 2>/dev/null || true;
rm -f '$RemoteBase/resume-daemon.pid' '$RemoteBase/resume-daemon.ssd.pid';
env AUDIOBOOK_POSITION_SOURCE='$PositionSource' AUDIOBOOK_RESTORE_ENABLED='$(if ($RestoreEnabled) { "1" } else { "0" })' AUDIOBOOK_TRACK_RESTORE_ENABLED='$trackRestoreValue' AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED='$directTrackSelectValue' AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED='$directTrackPreplayValue' AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES='$BookTitleDirectTrackMaxSwipes' AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS='$BookTitleDirectTrackVisibleRows' AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE='$BookTitleDirectTrackRowsPerSwipe' AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED='$bookTitleMemscanValue' AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED='$bookTitleDirectTrackCalibrateValue' AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED='$bookTitleDirectTrackRecoveryValue' AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS='$BookTitleDirectTrackRecoveryMaxSteps' AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH='$(if ($DisableBookTitlePathGuard) { "0" } else { "1" })' AUDIOBOOK_INTERVAL_SECONDS='$IntervalSeconds' AUDIOBOOK_IDLE_INTERVAL_SECONDS='$IdleIntervalSeconds' AUDIOBOOK_BOOK_TITLE_MARKER_IDLE_POLL_SECONDS='$BookTitleMarkerIdlePollSeconds' AUDIOBOOK_BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS='$BookTitleMarkerMusicPollSeconds' AUDIOBOOK_DIAGNOSTICS_INTERVAL_SECONDS='$DiagnosticsIntervalSeconds' AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS='$BookTitleAutostartDelaySeconds' AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS='$BookTitleContextSeconds' AUDIOBOOK_NEW_TRACK_COMMIT_MS='$NewTrackCommitMs' AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS='$RestoreRetryMaxAfterFailureSeconds' AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS='$FailedRestoreSkipLogBucketMs' AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS='$BookTitleRestoreLogBucketMs' AUDIOBOOK_RESUME_LOG_MAX_BYTES='$ResumeLogMaxBytes' AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED='$uiSeekFallbackValue' AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED='$uiSeekScreenGuardValue' AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS='$UiSeekScreenMinBarPixels' AUDIOBOOK_UI_SEEK_BAR_X_MIN='$UiSeekBarXMin' AUDIOBOOK_UI_SEEK_BAR_X_MAX='$UiSeekBarXMax' AUDIOBOOK_UI_SEEK_BAR_Y='$UiSeekBarY' AUDIOBOOK_UI_SEEK_VERIFY_TOLERANCE_MS='$UiSeekVerifyToleranceMs' AUDIOBOOK_UI_SEEK_TOUCH_FRAMES='$UiSeekTouchFrames' start-stop-daemon -S -b -m -p '$RemoteBase/resume-daemon.ssd.pid' -x '$RemoteBase/bin/r1_audiobook_resume_daemon.sh';
sleep 1;
echo '--- pid ---';
cat '$RemoteBase/resume-daemon.pid' '$RemoteBase/resume-daemon.ssd.pid' 2>/dev/null;
echo '--- process ---';
ps | grep r1_audiobook_resume_daemon | grep -v grep || true;
echo '--- log ---';
cat '$RemoteBase/resume-daemon.log' 2>/dev/null | tail -20;
"@

& $adbPath shell $installCommand
if ($LASTEXITCODE -ne 0) {
    throw "failed to install/start resume daemon"
}
