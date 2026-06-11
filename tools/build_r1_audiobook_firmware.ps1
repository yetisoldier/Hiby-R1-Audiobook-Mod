param(
    [Parameter(Mandatory=$false)]
    [string]$Rootfs = "work\original\rootfs.squashfs",

    [Parameter(Mandatory=$false)]
    [string]$XImage = "work\original\xImage",

    [Parameter(Mandatory=$false)]
    [string]$OutDir = "work\audiobook-firmware",

    [Parameter(Mandatory=$false)]
    [string]$OutputUpt = "work\audiobook-firmware\r1-audiobooks-dev-safe.upt",

    [Parameter(Mandatory=$false)]
    [string]$SquashfsTools = ".deps\squashfs\tools\squashfs-tools",

    [switch]$IncludeScannerAudiobookSkip,

    [switch]$IncludeExperimentalBookAudioShim,

    [switch]$IncludeAudiobookLauncherGenre,

    [switch]$IncludeAudiobookTitleAutoStartMarker,

    [switch]$IncludeSelectDispatchBranch,

    [switch]$IncludeAudiobookResumeRuntime,

    [Parameter(Mandatory=$false)]
    [string]$AudiobookResumeHelper = "work\device-audiobook-helper-20260609\audiobooks\bin\r1_audiobook_resume_helper",

    [Parameter(Mandatory=$false)]
    [string]$AudiobookResumeDaemon = "tools\r1_audiobook_resume_daemon.sh",

    [Parameter(Mandatory=$false)]
    [string]$AudiobookResumeCatalog = "",

    [switch]$IncludeAudiobookDbMaintenance,

    [Parameter(Mandatory=$false)]
    [string]$AudiobookDbMaintHelper = "work\native-db-maint\r1_audiobook_db_maint",

    [Parameter(Mandatory=$false)]
    [string]$AudiobookDbWatch = "tools\r1_audiobook_db_watch.sh",

    [Parameter(Mandatory=$false)]
    [string]$MediaDbSeed = "firmware\seed\usrlocal_media.seed.db",

    [Parameter(Mandatory=$false)]
    [string]$CustomVersionId = "1.6.4-audiobook",

    [Parameter(Mandatory=$false)]
    [string]$CustomVersionLabel = "HiBy R1 Audiobook FW 1.6.4",

    [Parameter(Mandatory=$false)]
    [string]$TouchNextEventSource = "",

    [Parameter(Mandatory=$false)]
    [string]$TouchFirstTrackEventSource = "",

    [Parameter(Mandatory=$false)]
    [string]$KeyNextEventSource = "",

    [Parameter(Mandatory=$false)]
    [string]$KeyPrevEventSource = ""
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

$rootfsPath = Resolve-PathStrict $Rootfs
$xImagePath = Resolve-PathStrict $XImage
$mksquashfs = Resolve-PathStrict (Join-Path $SquashfsTools "mksquashfs.exe")
$unsquashfs = Resolve-PathStrict (Join-Path $SquashfsTools "unsquashfs.exe")

if (Test-Path -LiteralPath $OutDir) {
    Remove-Item -Recurse -Force -LiteralPath $OutDir
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$rootTree = Join-Path $OutDir "squashfs-root"
$playerPath = Join-Path $rootTree "usr\bin\hiby_player"
$patchedPlayer = Join-Path $OutDir "hiby_player.audiobooks"
$newRootfs = Join-Path $OutDir "rootfs.squashfs"
$otaTree = Join-Path $OutDir "ota-tree"
$pseudoFile = Join-Path $OutDir "rootfs-pseudo.txt"

& $unsquashfs -d $rootTree $rootfsPath
if ($LASTEXITCODE -ne 0) {
    throw "unsquashfs failed"
}

$playerPatchArgs = @($playerPath, "-o", $patchedPlayer)
if ($IncludeScannerAudiobookSkip) {
    $playerPatchArgs += "--scan-skip"
}
if ($IncludeExperimentalBookAudioShim) {
    $playerPatchArgs += "--book-audio-shim"
}
if ($IncludeAudiobookLauncherGenre) {
    $playerPatchArgs += "--audiobook-launcher-genre"
}
if ($IncludeAudiobookTitleAutoStartMarker) {
    $playerPatchArgs += "--audiobook-title-autostart-marker"
}
if ($IncludeSelectDispatchBranch) {
    $playerPatchArgs += "--select-dispatch-branch"
}

python tools\patch_hiby_player.py @playerPatchArgs
Copy-Item -Force -LiteralPath $patchedPlayer -Destination $playerPath

python tools\patch_r1_resource_text.py $rootTree --about-model $CustomVersionLabel --product-version $CustomVersionId

$customVersionMarker = Join-Path $rootTree "etc\r1_audiobook_version"
@"
version=$CustomVersionId
label=$CustomVersionLabel
base_firmware=1.6
"@ | Set-Content -LiteralPath $customVersionMarker -Encoding ASCII

# Stock firmware ships the ADB startup helper as T90adb, but rcS only runs
# /etc/init.d/S??* scripts. Install the helper under an S-name for development
# builds, though the test device still requires manual ADB enabling after
# reboot/update, so release workflows must not rely on persistent ADB.
$adbBootScript = Join-Path $rootTree "etc\init.d\T90adb"
$persistentAdbBootScript = Join-Path $rootTree "etc\init.d\S90adb"
Copy-Item -Force -LiteralPath $adbBootScript -Destination $persistentAdbBootScript

if ($IncludeAudiobookResumeRuntime) {
    $resumeHelperPath = Resolve-PathStrict $AudiobookResumeHelper
    $resumeDaemonPath = Resolve-PathStrict $AudiobookResumeDaemon
    if ($TouchNextEventSource) {
        $touchNextEventPath = Resolve-PathStrict $TouchNextEventSource
    } else {
        $touchNextEventPath = Join-Path $OutDir "touch_next_event1.bin"
        python tools\adb_inject_touch_event.py --output $touchNextEventPath | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "failed to generate touch-next event stream"
        }
        $touchNextEventPath = Resolve-PathStrict $touchNextEventPath
    }
    if ($TouchFirstTrackEventSource) {
        $touchFirstTrackEventPath = Resolve-PathStrict $TouchFirstTrackEventSource
    } else {
        $touchFirstTrackEventPath = Join-Path $OutDir "touch_first_track_event1.bin"
        python tools\adb_inject_touch_event.py --x 203 --y 197 --output $touchFirstTrackEventPath | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "failed to generate first-track touch event stream"
        }
        $touchFirstTrackEventPath = Resolve-PathStrict $touchFirstTrackEventPath
    }
    $touchFirstTrackDownEventPath = Join-Path $OutDir "touch_first_track_down_event1.bin"
    python tools\adb_inject_touch_event.py --x 203 --y 197 --touch-phase down --output $touchFirstTrackDownEventPath | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate first-track down event stream"
    }
    $touchFirstTrackDownEventPath = Resolve-PathStrict $touchFirstTrackDownEventPath
    $touchFirstTrackMoveEventPath = Join-Path $OutDir "touch_first_track_move_event1.bin"
    python tools\adb_inject_touch_event.py --x 203 --y 197 --touch-phase move --output $touchFirstTrackMoveEventPath | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate first-track move event stream"
    }
    $touchFirstTrackMoveEventPath = Resolve-PathStrict $touchFirstTrackMoveEventPath
    $touchFirstTrackUpEventPath = Join-Path $OutDir "touch_first_track_up_event1.bin"
    python tools\adb_inject_touch_event.py --x 203 --y 197 --touch-phase up --output $touchFirstTrackUpEventPath | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate first-track up event stream"
    }
    $touchFirstTrackUpEventPath = Resolve-PathStrict $touchFirstTrackUpEventPath
    $touchBackEventPath = Join-Path $OutDir "touch_back_event1.bin"
    python tools\adb_inject_touch_event.py --x 30 --y 400 --to-x 360 --to-y 400 --drag-frames 18 --output $touchBackEventPath | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate back touch event stream"
    }
    $touchBackEventPath = Resolve-PathStrict $touchBackEventPath
    $touchTrackRowEventPaths = @($touchFirstTrackEventPath)
    $trackRowSpecs = @(
        @{ Name = "touch_track_row2_event1.bin"; X = 203; Y = 325 },
        @{ Name = "touch_track_row3_event1.bin"; X = 203; Y = 453 },
        @{ Name = "touch_track_row4_event1.bin"; X = 203; Y = 581 },
        @{ Name = "touch_track_row5_event1.bin"; X = 203; Y = 745 }
    )
    foreach ($spec in $trackRowSpecs) {
        $touchTrackRowEventPath = Join-Path $OutDir $spec.Name
        python tools\adb_inject_touch_event.py --x $spec.X --y $spec.Y --output $touchTrackRowEventPath | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "failed to generate track-row touch event stream $($spec.Name)"
        }
        $touchTrackRowEventPaths += (Resolve-PathStrict $touchTrackRowEventPath)
    }
    $touchTrackSwipeEventPaths = @()
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
    foreach ($spec in $swipePhaseSpecs) {
        $touchTrackSwipeEventPath = Join-Path $OutDir $spec.Name
        python tools\adb_inject_touch_event.py --x $spec.X --y $spec.Y --touch-phase $spec.Phase --output $touchTrackSwipeEventPath | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "failed to generate track-swipe event stream $($spec.Name)"
        }
        $touchTrackSwipeEventPaths += (Resolve-PathStrict $touchTrackSwipeEventPath)
    }
    if ($KeyNextEventSource) {
        $keyNextEventPath = Resolve-PathStrict $KeyNextEventSource
    } else {
        $keyNextEventPath = Join-Path $OutDir "key_next_event0.bin"
        python tools\adb_inject_touch_event.py --button next --output $keyNextEventPath | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "failed to generate key-next event stream"
        }
        $keyNextEventPath = Resolve-PathStrict $keyNextEventPath
    }
    if ($KeyPrevEventSource) {
        $keyPrevEventPath = Resolve-PathStrict $KeyPrevEventSource
    } else {
        $keyPrevEventPath = Join-Path $OutDir "key_prev_event2.bin"
        python tools\adb_inject_touch_event.py --button prev --output $keyPrevEventPath | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "failed to generate key-prev event stream"
        }
        $keyPrevEventPath = Resolve-PathStrict $keyPrevEventPath
    }
    $resumeCatalogPath = $null
    if ($AudiobookResumeCatalog) {
        $resumeCatalogPath = Resolve-PathStrict $AudiobookResumeCatalog
    }
    Copy-Item -Force -LiteralPath $resumeHelperPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_resume_helper")
    Copy-Item -Force -LiteralPath $resumeDaemonPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_resume_daemon.sh")
    Copy-Item -Force -LiteralPath $touchNextEventPath -Destination (Join-Path $rootTree "usr\bin\r1_touch_next_event1.bin")
    Copy-Item -Force -LiteralPath $touchFirstTrackEventPath -Destination (Join-Path $rootTree "usr\bin\r1_touch_first_track_event1.bin")
    Copy-Item -Force -LiteralPath $touchFirstTrackDownEventPath -Destination (Join-Path $rootTree "usr\bin\r1_touch_first_track_down_event1.bin")
    Copy-Item -Force -LiteralPath $touchFirstTrackMoveEventPath -Destination (Join-Path $rootTree "usr\bin\r1_touch_first_track_move_event1.bin")
    Copy-Item -Force -LiteralPath $touchFirstTrackUpEventPath -Destination (Join-Path $rootTree "usr\bin\r1_touch_first_track_up_event1.bin")
    Copy-Item -Force -LiteralPath $touchBackEventPath -Destination (Join-Path $rootTree "usr\bin\r1_touch_back_event1.bin")
    for ($i = 0; $i -lt $touchTrackRowEventPaths.Count; $i++) {
        $rowNumber = $i + 1
        Copy-Item -Force -LiteralPath $touchTrackRowEventPaths[$i] -Destination (Join-Path $rootTree "usr\bin\r1_touch_track_row${rowNumber}_event1.bin")
    }
    foreach ($path in $touchTrackSwipeEventPaths) {
        Copy-Item -Force -LiteralPath $path -Destination (Join-Path $rootTree ("usr\bin\r1_" + (Split-Path -Leaf $path)))
    }
    Copy-Item -Force -LiteralPath $keyNextEventPath -Destination (Join-Path $rootTree "usr\bin\r1_key_next_event0.bin")
    Copy-Item -Force -LiteralPath $keyPrevEventPath -Destination (Join-Path $rootTree "usr\bin\r1_key_prev_event2.bin")
    if ($resumeCatalogPath) {
        Copy-Item -Force -LiteralPath $resumeCatalogPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_catalog.tsv")
    }

    $resumeBootScript = Join-Path $rootTree "etc\init.d\S91audiobook_resume.sh"
    $resumeBootScriptText = @'
#!/bin/sh

BASE=/usr/data/audiobooks

clear_stock_audiobook_last_file() {
  user_ini=/usr/data/user.ini
  [ -f "$user_ini" ] || return 0

  hex=
  for byte in $(dd if="$user_ini" bs=1 skip=40 count=28 2>/dev/null | od -An -tx1); do
    hex=$hex$byte
  done

  case "$hex" in
    61003a005c0041007500640069006f0062006f006f006b007300*|41003a005c0041007500640069006f0062006f006f006b007300*|00003a005c0041007500640069006f0062006f006f006b007300*)
      if [ ! -f "$BASE/user.ini.before-stock-audiobook-last-clear" ]; then
        cp -f "$user_ini" "$BASE/user.ini.before-stock-audiobook-last-clear" 2>/dev/null || true
      fi
      dd if=/dev/zero of="$user_ini" bs=1 seek=40 count=320 conv=notrunc 2>/dev/null || true
      sync
      printf '%s cleared stock last audiobook path slot in /usr/data/user.ini\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')" >>"$BASE/boot-reset.log"
      ;;
  esac
}

if [ "$1" = stop ]; then
  start-stop-daemon -K -p "$BASE/resume-daemon.ssd.pid" 2>/dev/null || true
  exit 0
fi

if [ "$1" != start ]; then
  if [ -n "$1" ]; then
    exit 0
  fi
fi

mkdir -p "$BASE/bin" "$BASE/input" "$BASE/resume.d"
clear_stock_audiobook_last_file
cp -f /usr/bin/r1_audiobook_resume_helper "$BASE/bin/r1_audiobook_resume_helper"
cp -f /usr/bin/r1_audiobook_resume_daemon.sh "$BASE/bin/r1_audiobook_resume_daemon.sh"
cp -f /usr/bin/r1_touch_next_event1.bin "$BASE/input/touch_next_event1.bin"
cp -f /usr/bin/r1_touch_first_track_event1.bin "$BASE/input/touch_first_track_event1.bin"
cp -f /usr/bin/r1_touch_first_track_down_event1.bin "$BASE/input/touch_first_track_down_event1.bin"
cp -f /usr/bin/r1_touch_first_track_move_event1.bin "$BASE/input/touch_first_track_move_event1.bin"
cp -f /usr/bin/r1_touch_first_track_up_event1.bin "$BASE/input/touch_first_track_up_event1.bin"
cp -f /usr/bin/r1_touch_back_event1.bin "$BASE/input/touch_back_event1.bin"
cp -f /usr/bin/r1_touch_track_row1_event1.bin "$BASE/input/touch_track_row1_event1.bin"
cp -f /usr/bin/r1_touch_track_row2_event1.bin "$BASE/input/touch_track_row2_event1.bin"
cp -f /usr/bin/r1_touch_track_row3_event1.bin "$BASE/input/touch_track_row3_event1.bin"
cp -f /usr/bin/r1_touch_track_row4_event1.bin "$BASE/input/touch_track_row4_event1.bin"
cp -f /usr/bin/r1_touch_track_row5_event1.bin "$BASE/input/touch_track_row5_event1.bin"
cp -f /usr/bin/r1_touch_track_swipe_down_event1.bin "$BASE/input/touch_track_swipe_down_event1.bin"
cp -f /usr/bin/r1_touch_track_swipe_move1_event1.bin "$BASE/input/touch_track_swipe_move1_event1.bin"
cp -f /usr/bin/r1_touch_track_swipe_move2_event1.bin "$BASE/input/touch_track_swipe_move2_event1.bin"
cp -f /usr/bin/r1_touch_track_swipe_move3_event1.bin "$BASE/input/touch_track_swipe_move3_event1.bin"
cp -f /usr/bin/r1_touch_track_swipe_move4_event1.bin "$BASE/input/touch_track_swipe_move4_event1.bin"
cp -f /usr/bin/r1_touch_track_swipe_move5_event1.bin "$BASE/input/touch_track_swipe_move5_event1.bin"
cp -f /usr/bin/r1_touch_track_swipe_move6_event1.bin "$BASE/input/touch_track_swipe_move6_event1.bin"
cp -f /usr/bin/r1_touch_track_swipe_up_event1.bin "$BASE/input/touch_track_swipe_up_event1.bin"
cp -f /usr/bin/r1_key_next_event0.bin "$BASE/input/key_next_event0.bin"
cp -f /usr/bin/r1_key_prev_event2.bin "$BASE/input/key_prev_event2.bin"

if [ ! -s "$BASE/catalog.tsv" ]; then
  if [ -f /usr/bin/r1_audiobook_catalog.tsv ]; then
    cp -f /usr/bin/r1_audiobook_catalog.tsv "$BASE/catalog.tsv"
  fi
fi

chmod 755 "$BASE/bin/r1_audiobook_resume_helper" "$BASE/bin/r1_audiobook_resume_daemon.sh"
for file in "$BASE/input/"*.bin; do
  if [ -e "$file" ]; then
    chmod 644 "$file"
  fi
done

start-stop-daemon -K -p "$BASE/resume-daemon.ssd.pid" 2>/dev/null || true
rm -f "$BASE/resume-daemon.pid" "$BASE/resume-daemon.ssd.pid"

AUDIOBOOK_POSITION_SOURCE=memory
AUDIOBOOK_RESTORE_ENABLED=1
AUDIOBOOK_TRACK_RESTORE_ENABLED=1
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED=1
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED=1
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES=20
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS=5
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE=4
AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH=1
AUDIOBOOK_INTERVAL_SECONDS=1
AUDIOBOOK_IDLE_INTERVAL_SECONDS=3
AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS=1
AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS=300
AUDIOBOOK_NEW_TRACK_COMMIT_MS=15000
AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS=300
AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS=30000
AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS=5000
AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED=1
AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED=1
AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS=300
AUDIOBOOK_UI_SEEK_TOUCH_FRAMES=2
export AUDIOBOOK_POSITION_SOURCE AUDIOBOOK_RESTORE_ENABLED AUDIOBOOK_TRACK_RESTORE_ENABLED
export AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED
export AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE
export AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH
export AUDIOBOOK_INTERVAL_SECONDS AUDIOBOOK_IDLE_INTERVAL_SECONDS AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS
export AUDIOBOOK_NEW_TRACK_COMMIT_MS AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS
export AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED
export AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS AUDIOBOOK_UI_SEEK_TOUCH_FRAMES
start-stop-daemon -S -b -m -p "$BASE/resume-daemon.ssd.pid" -x /bin/sh -- "$BASE/bin/r1_audiobook_resume_daemon.sh" >>"$BASE/resume-daemon.stdout.log" 2>&1
'@
    $resumeBootScriptText = $resumeBootScriptText -replace "`r`n", "`n"
    [System.IO.File]::WriteAllText($resumeBootScript, $resumeBootScriptText, [System.Text.Encoding]::ASCII)
}

if ($IncludeAudiobookDbMaintenance) {
    $dbMaintHelperPath = Resolve-PathStrict $AudiobookDbMaintHelper
    $dbWatchPath = Resolve-PathStrict $AudiobookDbWatch
    $mediaDbSeedPath = Resolve-PathStrict $MediaDbSeed
    Copy-Item -Force -LiteralPath $dbMaintHelperPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_db_maint")
    Copy-Item -Force -LiteralPath $dbWatchPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_db_watch.sh")
    Copy-Item -Force -LiteralPath $mediaDbSeedPath -Destination (Join-Path $rootTree "usr\bin\r1_usrlocal_media_seed.db")

    $dbMaintBootScript = Join-Path $rootTree "etc\init.d\S92audiobook_db_maint.sh"
    $dbMaintBootScriptText = @'
#!/bin/sh

BASE=/usr/data/audiobooks

if [ "$1" = stop ]; then
  start-stop-daemon -K -p "$BASE/db-maint.ssd.pid" 2>/dev/null || true
  exit 0
fi

if [ "$1" != start ]; then
  if [ -n "$1" ]; then
    exit 0
  fi
fi

mkdir -p "$BASE/bin" "$BASE/resume.d"
cp -f /usr/bin/r1_audiobook_db_maint "$BASE/bin/r1_audiobook_db_maint"
cp -f /usr/bin/r1_audiobook_db_watch.sh "$BASE/bin/r1_audiobook_db_watch.sh"
cp -f /usr/bin/r1_usrlocal_media_seed.db "$BASE/bin/r1_usrlocal_media_seed.db"
chmod 755 "$BASE/bin/r1_audiobook_db_maint" "$BASE/bin/r1_audiobook_db_watch.sh"
chmod 644 "$BASE/bin/r1_usrlocal_media_seed.db"

start-stop-daemon -K -p "$BASE/db-maint.ssd.pid" 2>/dev/null || true
rm -f "$BASE/db-maint.pid" "$BASE/db-maint.ssd.pid"

AUDIOBOOK_DB_BOOT_DELAY_SECONDS=20
AUDIOBOOK_DB_INTERVAL_SECONDS=30
AUDIOBOOK_DB_STABLE_SECONDS=15
AUDIOBOOK_DB_FULL_REFRESH_INTERVAL_SECONDS=0
export AUDIOBOOK_DB_BOOT_DELAY_SECONDS AUDIOBOOK_DB_INTERVAL_SECONDS AUDIOBOOK_DB_STABLE_SECONDS
export AUDIOBOOK_DB_FULL_REFRESH_INTERVAL_SECONDS

start-stop-daemon -S -b -m -p "$BASE/db-maint.ssd.pid" -x /bin/sh -- "$BASE/bin/r1_audiobook_db_watch.sh" >>"$BASE/db-maint.stdout.log" 2>&1
'@
    $dbMaintBootScriptText = $dbMaintBootScriptText -replace "`r`n", "`n"
    [System.IO.File]::WriteAllText($dbMaintBootScript, $dbMaintBootScriptText, [System.Text.Encoding]::ASCII)
}

python tools\write_squashfs_pseudo_modes.py --rootfs $rootfsPath --unsquashfs $unsquashfs --output $pseudoFile
if ($LASTEXITCODE -ne 0) {
    throw "failed to generate stock SquashFS pseudo modes"
}

$newFileModeOverrides = @(
    @{ Path = "etc\init.d\S90adb"; Mode = "0755" },
    @{ Path = "etc\init.d\S91audiobook_resume.sh"; Mode = "0755" },
    @{ Path = "etc\init.d\S92audiobook_db_maint.sh"; Mode = "0755" },
    @{ Path = "etc\r1_audiobook_version"; Mode = "0644" },
    @{ Path = "usr\bin\r1_audiobook_resume_helper"; Mode = "0755" },
    @{ Path = "usr\bin\r1_audiobook_resume_daemon.sh"; Mode = "0755" },
    @{ Path = "usr\bin\r1_audiobook_db_maint"; Mode = "0755" },
    @{ Path = "usr\bin\r1_audiobook_db_watch.sh"; Mode = "0755" },
    @{ Path = "usr\bin\r1_usrlocal_media_seed.db"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_next_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_first_track_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_first_track_down_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_first_track_move_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_first_track_up_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_back_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_row1_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_row2_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_row3_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_row4_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_row5_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_swipe_down_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_swipe_move1_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_swipe_move2_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_swipe_move3_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_swipe_move4_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_swipe_move5_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_swipe_move6_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_touch_track_swipe_up_event1.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_key_next_event0.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_key_prev_event2.bin"; Mode = "0644" },
    @{ Path = "usr\bin\r1_audiobook_catalog.tsv"; Mode = "0644" }
)
$pseudoLines = foreach ($entry in $newFileModeOverrides) {
    $relativePath = $entry.Path
    if (Test-Path -LiteralPath (Join-Path $rootTree $relativePath)) {
        $squashPath = $relativePath.Replace("\", "/")
        "$squashPath m $($entry.Mode) 0 0"
    }
}
$pseudoLines | Add-Content -LiteralPath $pseudoFile -Encoding ASCII

& $mksquashfs $rootTree $newRootfs -comp lzo -b 131072 -no-progress -all-root -pf $pseudoFile
if ($LASTEXITCODE -ne 0) {
    throw "mksquashfs failed"
}

python tools\build_r1_upt.py --ximage $xImagePath --rootfs $newRootfs --output $OutputUpt --keep-tree $otaTree

Get-Item -LiteralPath $OutputUpt, $newRootfs
