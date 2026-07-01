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

    [switch]$IncludeAudiobookPrivateDirectRoute,

    [switch]$IncludeAudiobookNativeHubTitleRow,


    [switch]$AllowUnsafeNativeHubTitleRow,

    [switch]$IncludeAudiobookNativeHubLauncher,

    [switch]$IncludeAudiobookNativeHubFolderRows,

    [switch]$IncludeAudiobookNativeHubViewRows,

    [switch]$IncludeAudiobookTitleAutoStartMarker,

    [switch]$IncludeSelectDispatchBranch,

    [switch]$IncludeAudiobookLauncherIcon,

    [switch]$EnableBootAdb,

    [switch]$DisableBatdLogger,

    [switch]$UnlockNativeDsd,

    [switch]$EnableBluetoothSbcXq,

    [switch]$UnlockUsbDacMode,

    [switch]$IncludeAudiobookResumeRuntime,

    [switch]$UseConservativeResumeRuntime,

    [Parameter(Mandatory=$false)]
    [string]$AudiobookResumeHelper = "work\device-audiobook-helper-20260609\audiobooks\bin\r1_audiobook_resume_helper",

    [Parameter(Mandatory=$false)]
    [string]$AudiobookMemscanHelper = "work\native-memscan\r1_audiobook_memscan",

    [Parameter(Mandatory=$false)]
    [string]$AudiobookDirectOpenHelper = "work\native-direct-open\r1_audiobook_direct_open",

    [Parameter(Mandatory=$false)]
    [string]$AudiobookBookmarkMonitorHelper = "work\native-bookmark-monitor\r1_audiobook_bookmark_monitor",

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
    [string]$AudiobookRefreshRequest = "tools\r1_audiobook_refresh.sh",

    [Parameter(Mandatory=$false)]
    [string]$MediaDbSeed = "firmware\seed\usrlocal_media.seed.db",

    [Parameter(Mandatory=$false)]
    [string]$CustomVersionId = "1.6.16.5-audiobook",

    [Parameter(Mandatory=$false)]
    [string]$CustomVersionLabel = "HiBy R1 Audiobook FW 1.6.16.5",

    [Parameter(Mandatory=$false)]
    [int]$OtaVersion = 0,

    [Parameter(Mandatory=$false)]
    [string]$OtaSite = "",

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

if ($OtaVersion -lt 0) {
    throw "OtaVersion must be non-negative"
}

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

if ($IncludeAudiobookNativeHubTitleRow -and !$AllowUnsafeNativeHubTitleRow) {
    throw "IncludeAudiobookNativeHubTitleRow is unsafe after live testing rebooted the R1. Pass -AllowUnsafeNativeHubTitleRow only for controlled flash-test builds."
}
if ($IncludeAudiobookNativeHubViewRows -and $IncludeAudiobookLauncherGenre) {
    throw "IncludeAudiobookNativeHubViewRows and IncludeAudiobookLauncherGenre are alternative audiobook entry paths. Choose only one."
}

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
if ($IncludeAudiobookPrivateDirectRoute) {
    $playerPatchArgs += "--audiobook-private-direct-route"
}
if ($IncludeAudiobookNativeHubTitleRow) {
    $playerPatchArgs += "--audiobook-native-hub-title-row"
}
if ($IncludeAudiobookNativeHubLauncher) {
    $playerPatchArgs += "--audiobook-native-hub-launcher"
}
if ($IncludeAudiobookNativeHubFolderRows) {
    $playerPatchArgs += "--audiobook-native-hub-folder-rows"
}
if ($IncludeAudiobookNativeHubViewRows) {
    if (!$IncludeAudiobookNativeHubLauncher) {
        $IncludeAudiobookNativeHubLauncher = $true
        $playerPatchArgs += "--audiobook-native-hub-launcher"
    }
    $playerPatchArgs += "--audiobook-native-hub-view-rows"
}
if ($IncludeAudiobookTitleAutoStartMarker) {
    $playerPatchArgs += "--audiobook-title-autostart-marker"
}
if ($IncludeSelectDispatchBranch) {
    $playerPatchArgs += "--select-dispatch-branch"
}

python tools\patch_hiby_player.py @playerPatchArgs
if ($LASTEXITCODE -ne 0) {
    throw "failed to patch hiby_player"
}
Copy-Item -Force -LiteralPath $patchedPlayer -Destination $playerPath

$resourcePatchArgs = @($rootTree, "--about-model", $CustomVersionLabel, "--product-version", $CustomVersionId)
if ($IncludeAudiobookNativeHubViewRows) {
    $resourcePatchArgs += "--audiobook-native-hub-view-labels"
} elseif ($IncludeAudiobookNativeHubTitleRow -or $IncludeAudiobookNativeHubFolderRows) {
    $resourcePatchArgs += "--audiobook-native-hub-labels"
}
python tools\patch_r1_resource_text.py @resourcePatchArgs
if ($LASTEXITCODE -ne 0) {
    throw "failed to patch resource text"
}
if ($IncludeAudiobookLauncherIcon) {
    python tools\generate_audiobook_launcher_icons.py $rootTree
    if ($LASTEXITCODE -ne 0) {
        throw "failed to generate audiobook launcher icons"
    }
}

$otaInfoPath = Join-Path $rootTree "etc\ota_info"
$effectiveOtaSite = "/data/autoupdate/autoupdate"
if (Test-Path -LiteralPath $otaInfoPath) {
    $otaInfoText = Get-Content -Raw -LiteralPath $otaInfoPath
    $otaSiteMatch = [regex]::Match($otaInfoText, "(?m)^ota_site=(.+)$")
    if ($otaSiteMatch.Success) {
        $effectiveOtaSite = $otaSiteMatch.Groups[1].Value.Trim()
    }
}
if ($OtaSite) {
    $effectiveOtaSite = $OtaSite
}
if (($OtaVersion -ne 0) -or $OtaSite) {
    $newOtaInfoText = "ota_version=$OtaVersion`nota_site=$effectiveOtaSite`n"
    [System.IO.File]::WriteAllText($otaInfoPath, $newOtaInfoText, [System.Text.Encoding]::ASCII)
}

$audioUnlockArgs = @($rootTree)
if ($UnlockNativeDsd) {
    $audioUnlockArgs += "--native-dsd"
}
if ($EnableBluetoothSbcXq) {
    $audioUnlockArgs += "--sbc-xq"
}
if ($UnlockUsbDacMode) {
    $audioUnlockArgs += "--usb-dac"
}
if ($audioUnlockArgs.Count -gt 1) {
    python tools\patch_r1_audio_feature_unlocks.py @audioUnlockArgs
    if ($LASTEXITCODE -ne 0) {
        throw "failed to patch audio feature unlocks"
    }
}

$customVersionMarker = Join-Path $rootTree "etc\r1_audiobook_version"
$audiobookEntryMarker = "stock-book"
if ($IncludeAudiobookLauncherGenre) {
    $audiobookEntryMarker = "direct-genre"
}
if ($IncludeAudiobookPrivateDirectRoute) {
    $audiobookEntryMarker = "direct-private-route"
}
if ($IncludeAudiobookNativeHubTitleRow) {
    $audiobookEntryMarker = "native-hub-title-row"
}
if ($IncludeAudiobookNativeHubLauncher) {
    $audiobookEntryMarker = "native-hub-launcher-title-row"
}
if ($IncludeAudiobookNativeHubFolderRows) {
    if ($IncludeAudiobookNativeHubLauncher) {
        $audiobookEntryMarker = "native-hub-launcher-title-folders"
    } else {
        $audiobookEntryMarker = "native-hub-title-folders"
    }
}
if ($IncludeAudiobookNativeHubViewRows) {
    $audiobookEntryMarker = "native-hub-view-rows"
}
$bootAdbMarker = if ($EnableBootAdb) { "enabled" } else { "disabled" }
$batdLoggerMarker = if ($DisableBatdLogger) { "disabled" } else { "enabled" }
$launcherIconMarker = if ($IncludeAudiobookLauncherIcon) { "audiobook" } else { "stock-book" }
$nativeDsdMarker = if ($UnlockNativeDsd) { "enabled" } else { "stock" }
$sbcXqMarker = if ($EnableBluetoothSbcXq) { "enabled" } else { "stock" }
$usbDacMarker = if ($UnlockUsbDacMode) { "enabled" } else { "stock" }
$nativeHubLauncherMarker = if ($IncludeAudiobookNativeHubLauncher) { "enabled" } else { "disabled" }
$nativeHubFolderRowsMarker = if ($IncludeAudiobookNativeHubFolderRows) { "enabled" } else { "disabled" }
$nativeHubViewRowsMarker = if ($IncludeAudiobookNativeHubViewRows) { "enabled" } else { "disabled" }
$audiobookViewGenerationEnabled = if ($IncludeAudiobookNativeHubViewRows) { "1" } else { "0" }
$resumeRuntimeProfileMarker = if ($UseConservativeResumeRuntime) { "conservative" } else { "direct" }
@"
version=$CustomVersionId
label=$CustomVersionLabel
base_firmware=1.6
ota_version=$OtaVersion
ota_site=$effectiveOtaSite
audiobook_entry=$audiobookEntryMarker
boot_adb=$bootAdbMarker
batd_logger=$batdLoggerMarker
launcher_icon=$launcherIconMarker
native_dsd=$nativeDsdMarker
bluetooth_sbc_xq=$sbcXqMarker
usb_dac_mode=$usbDacMarker
native_hub_launcher=$nativeHubLauncherMarker
native_hub_folder_rows=$nativeHubFolderRowsMarker
native_hub_view_rows=$nativeHubViewRowsMarker
resume_runtime_profile=$resumeRuntimeProfileMarker
"@ | Set-Content -LiteralPath $customVersionMarker -Encoding ASCII

# Stock R1 1.6 includes a guarded batd launch block in hiby_player.sh. If a batd
# binary is present, it writes batlog.txt to the SD card every five seconds.
# Keep this as an explicit build option until it has been battery/playback
# tested on-device with music and audiobooks.
if ($DisableBatdLogger) {
    $playerLaunchScript = Join-Path $rootTree "usr\bin\hiby_player.sh"
    $launchText = Get-Content -Raw -LiteralPath $playerLaunchScript
    $launchText = $launchText -replace "`r`n", "`n"
    $launchText = $launchText -replace "`r", "`n"
    $batdBlock = @'
if [ -f "/usr/bin/batd" ]; then
killall    batd    &>/dev/null
killall -9 batd    &>/dev/null
/usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt &
fi

'@
    $batdBlock = $batdBlock -replace "`r`n", "`n"
    $batdBlock = $batdBlock -replace "`r", "`n"
    if (-not $launchText.Contains($batdBlock)) {
        throw "Expected batd launch block not found in $playerLaunchScript"
    }
    $launchText = $launchText.Replace($batdBlock, "")
    if ($launchText.Contains("/usr/bin/batd -v -s -t5 -o /mnt/sd_0/batlog.txt")) {
        throw "batd SD logger command still present in $playerLaunchScript"
    }
    [System.IO.File]::WriteAllText($playerLaunchScript, $launchText, [System.Text.Encoding]::ASCII)
}

# Stock firmware ships the ADB startup helper as T90adb, but rcS only runs
# /etc/init.d/S??* scripts. Keep boot ADB opt-in so shareable release builds do
# not silently expose a debug bridge after reboot.
if ($EnableBootAdb) {
    $persistentAdbBootScript = Join-Path $rootTree "etc\init.d\S90adb"
    $persistentAdbBootScriptText = @'
#!/bin/sh
#
# Development-only boot ADB wrapper.
# The stock helper is T90adb, but rcS only starts S??* scripts. This wrapper
# starts ADB only when the stock UI setting System -> USB working mode is Device.
#

read_usb_working_mode() {
    if [ ! -f /usr/data/user.ini ]; then
        echo ""
        return
    fi

    set -- $(dd if=/usr/data/user.ini bs=1 skip=1856 count=1 2>/dev/null | od -An -t u1 2>/dev/null)
    echo "${1:-}"
}

case "$1" in
  start)
    mode=$(read_usb_working_mode)
    if [ "$mode" != "1" ]; then
        echo "Skip boot adb: usb_working_mode=$mode"
        exit 0
    fi
    /etc/init.d/T90adb start
    ;;
  stop)
    /etc/init.d/T90adb stop
    ;;
  restart|reload)
    /etc/init.d/T90adb stop
    /etc/init.d/T90adb start
    ;;
  *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
esac

exit $?
'@
    $persistentAdbBootScriptText = $persistentAdbBootScriptText -replace "`r`n", "`n"
    $persistentAdbBootScriptText = $persistentAdbBootScriptText -replace "`r", "`n"
    [System.IO.File]::WriteAllText($persistentAdbBootScript, $persistentAdbBootScriptText, [System.Text.Encoding]::ASCII)
}

if ($IncludeAudiobookResumeRuntime) {
    $resumeHelperPath = Resolve-PathStrict $AudiobookResumeHelper
    $memscanHelperPath = Resolve-PathStrict $AudiobookMemscanHelper
    $directOpenHelperPath = Resolve-PathStrict $AudiobookDirectOpenHelper
    $bookmarkMonitorHelperPath = Resolve-PathStrict $AudiobookBookmarkMonitorHelper
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
    Copy-Item -Force -LiteralPath $memscanHelperPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_memscan")
    Copy-Item -Force -LiteralPath $directOpenHelperPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_direct_open")
    Copy-Item -Force -LiteralPath $bookmarkMonitorHelperPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_bookmark_monitor")
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
    $audiobookTrackRestoreEnabled = if ($UseConservativeResumeRuntime) { "0" } else { "1" }
    $audiobookBookTitleAutostartEnabled = if ($UseConservativeResumeRuntime) { "0" } else { "1" }
    $audiobookDirectTrackSelectEnabled = if ($UseConservativeResumeRuntime) { "0" } else { "1" }
    $audiobookDirectTrackPreplayEnabled = if ($UseConservativeResumeRuntime) { "0" } else { "1" }
    $audiobookBookTitleMemscanEnabled = if ($UseConservativeResumeRuntime) { "0" } else { "1" }
    $audiobookDirectTrackCalibrateEnabled = if ($UseConservativeResumeRuntime) { "0" } else { "1" }
    $audiobookDirectTrackRecoveryEnabled = if ($UseConservativeResumeRuntime) { "0" } else { "1" }
    $audiobookDirectOpenEnabled = if ($UseConservativeResumeRuntime) { "0" } else { "1" }
    $audiobookUiSeekFallbackEnabled = if ($UseConservativeResumeRuntime) { "0" } else { "1" }
    $audiobookBackGuardEnabled = if ($UseConservativeResumeRuntime -or $IncludeAudiobookNativeHubLauncher) { "0" } else { "1" }
    $audiobookFirstTrackEntryRestoreEnabled = if (!$UseConservativeResumeRuntime -and $IncludeAudiobookNativeHubViewRows) { "1" } else { "0" }
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
  start-stop-daemon -K -p "$BASE/bookmark-monitor.pid" 2>/dev/null || true
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
cp -f /usr/bin/r1_audiobook_memscan "$BASE/bin/r1_audiobook_memscan"
cp -f /usr/bin/r1_audiobook_direct_open "$BASE/bin/r1_audiobook_direct_open"
cp -f /usr/bin/r1_audiobook_bookmark_monitor "$BASE/bin/r1_audiobook_bookmark_monitor"
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

chmod 755 "$BASE/bin/r1_audiobook_resume_helper" "$BASE/bin/r1_audiobook_memscan" "$BASE/bin/r1_audiobook_direct_open" "$BASE/bin/r1_audiobook_bookmark_monitor" "$BASE/bin/r1_audiobook_resume_daemon.sh"
for file in "$BASE/input/"*.bin; do
  if [ -e "$file" ]; then
    chmod 644 "$file"
  fi
done

old_pid=$(cat "$BASE/resume-daemon.pid" 2>/dev/null || true)
[ -n "$old_pid" ] && kill "$old_pid" 2>/dev/null || true
start-stop-daemon -K -p "$BASE/resume-daemon.ssd.pid" 2>/dev/null || true
start-stop-daemon -K -p "$BASE/bookmark-monitor.pid" 2>/dev/null || true
sleep 1
[ -n "$old_pid" ] && kill -9 "$old_pid" 2>/dev/null || true
rm -f "$BASE/resume-daemon.pid" "$BASE/resume-daemon.ssd.pid" "$BASE/bookmark-monitor.pid"

AUDIOBOOK_POSITION_SOURCE=memory
AUDIOBOOK_RESTORE_ENABLED=1
AUDIOBOOK_TRACK_RESTORE_ENABLED=__AUDIOBOOK_TRACK_RESTORE_ENABLED__
AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED=__AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED__
AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_MAX_MS=15000
AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED=__AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED=__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED=__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES=20
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS=5
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE=4
AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED=__AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED=__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED=__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED__
AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS=20
AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED=__AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED__
AUDIOBOOK_DIRECT_OPEN_PROBE_ADDR=0x760708
AUDIOBOOK_DIRECT_OPEN_SCRATCH_ADDR=0x8e4400
AUDIOBOOK_DIRECT_OPEN_TIMEOUT_MS=6000
AUDIOBOOK_DIRECT_OPEN_ARM_DELAY_US=200000
AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH=1
AUDIOBOOK_INTERVAL_SECONDS=2
AUDIOBOOK_IDLE_INTERVAL_SECONDS=5
AUDIOBOOK_BOOK_TITLE_MARKER_IDLE_POLL_SECONDS=5
AUDIOBOOK_BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS=15
AUDIOBOOK_BOOK_TITLE_CONTEXT_SCREEN_PROBE_SECONDS=2
AUDIOBOOK_DIAGNOSTICS_INTERVAL_SECONDS=60
AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS=1
AUDIOBOOK_BOOK_TITLE_LAUNCHER_TRACKLIST_WAIT_SECONDS=4
AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS=300
AUDIOBOOK_SAVE_BUCKET_MS=15000
AUDIOBOOK_NEW_TRACK_COMMIT_MS=15000
AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS=300
AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS=30000
AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS=5000
AUDIOBOOK_RESUME_LOG_MAX_BYTES=524288
AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED=__AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED__
AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED=1
AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS=300
AUDIOBOOK_UI_SEEK_TOUCH_FRAMES=2
AUDIOBOOK_BACK_GUARD_ENABLED=__AUDIOBOOK_BACK_GUARD_ENABLED__
AUDIOBOOK_BACK_GUARD_WINDOW_SECONDS=60
AUDIOBOOK_BACK_GUARD_AFTER_SCREEN_SECONDS=8
AUDIOBOOK_BACK_GUARD_IDLE_INTERVAL_SECONDS=1
AUDIOBOOK_BACK_GUARD_EXTRA_BACKS=2
export AUDIOBOOK_POSITION_SOURCE AUDIOBOOK_RESTORE_ENABLED AUDIOBOOK_TRACK_RESTORE_ENABLED
export AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_MAX_MS
export AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED
export AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED
export AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_MAX_SWIPES AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_VISIBLE_ROWS AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_ROWS_PER_SWIPE
export AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED
export AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_MAX_STEPS
export AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED AUDIOBOOK_DIRECT_OPEN_PROBE_ADDR AUDIOBOOK_DIRECT_OPEN_SCRATCH_ADDR
export AUDIOBOOK_DIRECT_OPEN_TIMEOUT_MS AUDIOBOOK_DIRECT_OPEN_ARM_DELAY_US
export AUDIOBOOK_BOOK_TITLE_AUTOSTART_REQUIRE_PATH
export AUDIOBOOK_INTERVAL_SECONDS AUDIOBOOK_IDLE_INTERVAL_SECONDS AUDIOBOOK_BOOK_TITLE_MARKER_IDLE_POLL_SECONDS AUDIOBOOK_BOOK_TITLE_MARKER_MUSIC_POLL_SECONDS
export AUDIOBOOK_BOOK_TITLE_CONTEXT_SCREEN_PROBE_SECONDS
export AUDIOBOOK_DIAGNOSTICS_INTERVAL_SECONDS AUDIOBOOK_BOOK_TITLE_AUTOSTART_DELAY_SECONDS
export AUDIOBOOK_BOOK_TITLE_LAUNCHER_TRACKLIST_WAIT_SECONDS
export AUDIOBOOK_BOOK_TITLE_CONTEXT_SECONDS
export AUDIOBOOK_SAVE_BUCKET_MS AUDIOBOOK_NEW_TRACK_COMMIT_MS AUDIOBOOK_RESTORE_RETRY_MAX_AFTER_FAILURE_SECONDS AUDIOBOOK_FAILED_RESTORE_SKIP_LOG_BUCKET_MS
export AUDIOBOOK_BOOK_TITLE_RESTORE_LOG_BUCKET_MS AUDIOBOOK_RESUME_LOG_MAX_BYTES AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED
export AUDIOBOOK_UI_SEEK_SCREEN_GUARD_ENABLED AUDIOBOOK_UI_SEEK_SCREEN_MIN_BAR_PIXELS AUDIOBOOK_UI_SEEK_TOUCH_FRAMES
export AUDIOBOOK_BACK_GUARD_ENABLED AUDIOBOOK_BACK_GUARD_WINDOW_SECONDS AUDIOBOOK_BACK_GUARD_AFTER_SCREEN_SECONDS
export AUDIOBOOK_BACK_GUARD_IDLE_INTERVAL_SECONDS
export AUDIOBOOK_BACK_GUARD_EXTRA_BACKS
start-stop-daemon -S -b -m -p "$BASE/resume-daemon.ssd.pid" -x /bin/sh -- "$BASE/bin/r1_audiobook_resume_daemon.sh" >>"$BASE/resume-daemon.stdout.log" 2>&1
start-stop-daemon -S -b -m -p "$BASE/bookmark-monitor.pid" -x "$BASE/bin/r1_audiobook_bookmark_monitor" -- --event /dev/input/event1 --request "$BASE/bookmark.request" --user-ini /usr/data/user.ini >>"$BASE/bookmark-monitor.stdout.log" 2>&1
'@
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_BACK_GUARD_ENABLED__", $audiobookBackGuardEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_TRACK_RESTORE_FIRST_TRACK_ENTRY_ENABLED__", $audiobookFirstTrackEntryRestoreEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_TRACK_RESTORE_ENABLED__", $audiobookTrackRestoreEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_BOOK_TITLE_AUTOSTART_ENABLED__", $audiobookBookTitleAutostartEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_SELECT_ENABLED__", $audiobookDirectTrackSelectEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_PREPLAY_ENABLED__", $audiobookDirectTrackPreplayEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_BOOK_TITLE_MEMSCAN_ENABLED__", $audiobookBookTitleMemscanEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_CALIBRATE_ENABLED__", $audiobookDirectTrackCalibrateEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_BOOK_TITLE_DIRECT_TRACK_RECOVERY_TRANSPORT_ENABLED__", $audiobookDirectTrackRecoveryEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_BOOK_TITLE_DIRECT_OPEN_ENABLED__", $audiobookDirectOpenEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "__AUDIOBOOK_UI_SEEK_FALLBACK_ENABLED__", $audiobookUiSeekFallbackEnabled
    $resumeBootScriptText = $resumeBootScriptText -replace "`r`n", "`n"
    [System.IO.File]::WriteAllText($resumeBootScript, $resumeBootScriptText, [System.Text.Encoding]::ASCII)
}

if ($IncludeAudiobookDbMaintenance) {
    $dbMaintHelperPath = Resolve-PathStrict $AudiobookDbMaintHelper
    $dbWatchPath = Resolve-PathStrict $AudiobookDbWatch
    $dbRefreshPath = Resolve-PathStrict $AudiobookRefreshRequest
    $mediaDbSeedPath = Resolve-PathStrict $MediaDbSeed
    Copy-Item -Force -LiteralPath $dbMaintHelperPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_db_maint")
    Copy-Item -Force -LiteralPath $dbWatchPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_db_watch.sh")
    Copy-Item -Force -LiteralPath $dbRefreshPath -Destination (Join-Path $rootTree "usr\bin\r1_audiobook_refresh.sh")
    Copy-Item -Force -LiteralPath $mediaDbSeedPath -Destination (Join-Path $rootTree "usr\bin\r1_usrlocal_media_seed.db")

    $dbMaintBootScript = Join-Path $rootTree "etc\init.d\S92audiobook_db_maint.sh"
$dbMaintBootScriptText = @'
#!/bin/sh

BASE=/usr/data/audiobooks

db_watch_pid_is_live() {
  check_pid=$1
  [ -n "$check_pid" ] || return 1
  [ -d "/proc/$check_pid" ] || return 1

  if [ -r "/proc/$check_pid/cmdline" ]; then
    cmdline=$(tr '\000' ' ' <"/proc/$check_pid/cmdline" 2>/dev/null || true)
    case "$cmdline" in
      *r1_audiobook_db_watch.sh*) return 0 ;;
    esac
  fi

  ps_line=$(ps | awk -v pid="$check_pid" '$1 == pid { $1=""; print }' 2>/dev/null | head -n 1)
  case "$ps_line" in
    *r1_audiobook_db_watch.sh*) return 0 ;;
  esac
  return 1
}

stop_db_watch() {
  old_pid=$(cat "$BASE/db-maint.ssd.pid" 2>/dev/null || cat "$BASE/db-maint.lock/pid" 2>/dev/null || true)
  case "$old_pid" in
    ''|*[!0-9]*) old_pid= ;;
  esac

  start-stop-daemon -K -p "$BASE/db-maint.ssd.pid" 2>/dev/null || true

  if [ -n "$old_pid" ]; then
    wait_count=0
    while [ "$wait_count" -lt 3 ] && db_watch_pid_is_live "$old_pid"; do
      sleep 1
      wait_count=$((wait_count + 1))
    done
    if db_watch_pid_is_live "$old_pid"; then
      kill -9 "$old_pid" 2>/dev/null || true
      sleep 1
    fi
  fi

  if [ -z "$old_pid" ] || ! db_watch_pid_is_live "$old_pid"; then
    rm -rf "$BASE/db-maint.lock" 2>/dev/null || true
  fi
  rm -f "$BASE/db-maint.pid" "$BASE/db-maint.ssd.pid"
}

if [ "$1" = stop ]; then
  stop_db_watch
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

stop_db_watch

AUDIOBOOK_DB_BOOT_DELAY_SECONDS=20
AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS=180
AUDIOBOOK_DB_STABLE_POLL_SECONDS=3
AUDIOBOOK_DB_INTERVAL_SECONDS=30
AUDIOBOOK_DB_STABLE_SECONDS=15
AUDIOBOOK_DB_FULL_REFRESH_INTERVAL_SECONDS=0
AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES=524288
AUDIOBOOK_DB_RUN_ON_MTIME_ONLY=0
AUDIOBOOK_DB_MTIME_ONLY_MIN_RERUN_SECONDS=0
AUDIOBOOK_DB_ZERO_AUDIO_RETRY_TIMEOUT_SECONDS=600
AUDIOBOOK_DB_ZERO_AUDIO_RETRY_POLL_SECONDS=5
AUDIOBOOK_DB_LOCKED_DB_RETRY_TIMEOUT_SECONDS=600
AUDIOBOOK_DB_LOCKED_DB_RETRY_POLL_SECONDS=5
AUDIOBOOK_VIEW_GENERATION_ENABLED=__AUDIOBOOK_VIEW_GENERATION_ENABLED__
export AUDIOBOOK_DB_BOOT_DELAY_SECONDS AUDIOBOOK_DB_BOOT_STABLE_TIMEOUT_SECONDS AUDIOBOOK_DB_STABLE_POLL_SECONDS
export AUDIOBOOK_DB_INTERVAL_SECONDS AUDIOBOOK_DB_STABLE_SECONDS
export AUDIOBOOK_DB_FULL_REFRESH_INTERVAL_SECONDS AUDIOBOOK_DB_MAINT_LOG_MAX_BYTES
export AUDIOBOOK_DB_RUN_ON_MTIME_ONLY AUDIOBOOK_DB_MTIME_ONLY_MIN_RERUN_SECONDS
export AUDIOBOOK_DB_ZERO_AUDIO_RETRY_TIMEOUT_SECONDS AUDIOBOOK_DB_ZERO_AUDIO_RETRY_POLL_SECONDS
export AUDIOBOOK_DB_LOCKED_DB_RETRY_TIMEOUT_SECONDS AUDIOBOOK_DB_LOCKED_DB_RETRY_POLL_SECONDS
export AUDIOBOOK_VIEW_GENERATION_ENABLED

start-stop-daemon -S -b -m -p "$BASE/db-maint.ssd.pid" -x /bin/sh -- "$BASE/bin/r1_audiobook_db_watch.sh" >>"$BASE/db-maint.stdout.log" 2>&1
'@
    $dbMaintBootScriptText = $dbMaintBootScriptText -replace "__AUDIOBOOK_VIEW_GENERATION_ENABLED__", $audiobookViewGenerationEnabled
    $dbMaintBootScriptText = $dbMaintBootScriptText -replace "`r`n", "`n"
    [System.IO.File]::WriteAllText($dbMaintBootScript, $dbMaintBootScriptText, [System.Text.Encoding]::ASCII)
}

python tools\write_squashfs_pseudo_modes.py --rootfs $rootfsPath --unsquashfs $unsquashfs --output $pseudoFile
if ($LASTEXITCODE -ne 0) {
    throw "failed to generate stock SquashFS pseudo modes"
}

$newFileModeOverrides = @()
if ($EnableBootAdb) {
    $newFileModeOverrides += @{ Path = "etc\init.d\S90adb"; Mode = "0755" }
}
$newFileModeOverrides += @(
    @{ Path = "etc\init.d\S91audiobook_resume.sh"; Mode = "0755" },
    @{ Path = "etc\init.d\S92audiobook_db_maint.sh"; Mode = "0755" },
    @{ Path = "etc\r1_audiobook_version"; Mode = "0644" },
    @{ Path = "usr\bin\r1_audiobook_resume_helper"; Mode = "0755" },
    @{ Path = "usr\bin\r1_audiobook_direct_open"; Mode = "0755" },
    @{ Path = "usr\bin\r1_audiobook_bookmark_monitor"; Mode = "0755" },
    @{ Path = "usr\bin\r1_audiobook_resume_daemon.sh"; Mode = "0755" },
    @{ Path = "usr\bin\r1_audiobook_db_maint"; Mode = "0755" },
    @{ Path = "usr\bin\r1_audiobook_db_watch.sh"; Mode = "0755" },
    @{ Path = "usr\bin\r1_audiobook_refresh.sh"; Mode = "0755" },
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

python tools\build_r1_upt.py --ximage $xImagePath --rootfs $newRootfs --output $OutputUpt --keep-tree $otaTree --ota-version $OtaVersion

Get-Item -LiteralPath $OutputUpt, $newRootfs
