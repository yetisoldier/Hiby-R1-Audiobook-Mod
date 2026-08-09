param(
    [Parameter(Mandatory=$false)]
    [switch]$SkipDbFixtures,

    [Parameter(Mandatory=$false)]
    [string]$Distro = "Ubuntu-24.04",

    [Parameter(Mandatory=$false)]
    [string]$WindowsHelper = "work\native-db-maint\r1_audiobook_db_maint_win_test.exe",

    [Parameter(Mandatory=$false)]
    [string]$MipsHelper = "work\native-db-maint\r1_audiobook_db_maint"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Step([string]$Name) {
    Write-Host ""
    Write-Host "== $Name =="
}

function Invoke-Checked([scriptblock]$Command, [string]$Name) {
    Step $Name
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

function Invoke-Python {
    if (Get-Command py -ErrorAction SilentlyContinue) {
        & py -3 @args
    } else {
        & python @args
    }
}

Step "PowerShell parser"
$psFiles = @(
    "tools\adb_archive_audiobook_dev_artifacts.ps1",
    "tools\adb_build_release_audiobook_db.ps1",
    "tools\adb_collect_audiobook_resume_debug.ps1",
    "tools\adb_collect_r1_state.ps1",
    "tools\adb_hold_hiby_player.ps1",
    "tools\adb_probe_usb_mode_toggle.ps1",
    "tools\compare_r1_settings_snapshots.ps1",
    "tools\adb_install_audiobook_resume_runtime.ps1",
    "tools\adb_live_audiobook_smoke.ps1",
    "tools\adb_install_release_audiobook_db.ps1",
    "tools\adb_manage_boot_adb.ps1",
    "tools\adb_monitor_r1_runtime.ps1",
    "tools\adb_run_audiobook_route_matrix_tests.ps1",
    "tools\adb_snapshot_r1_settings.ps1",
    "tools\adb_stage_verified_firmware.ps1",
    "tools\adb_verify_installed_audiobook_release.ps1",
    "tools\build_r1_audiobook_firmware.ps1",
    "tools\build_r1_db_maint_helper.ps1",
    "tools\build_r1_direct_open_helper.ps1",
    "tools\extract_r1_firmware.ps1",
    "tools\install_mips_binutils_wsl.ps1",
    "tools\mips_objdump_wsl.ps1",
    "tools\publish_github_release.ps1",
    "tools\stage_r1_firmware_package.ps1",
    "tools\test_r1_db_maint_qemu_wsl.ps1",
    "tools\test_music_catalog_cleanup.ps1",
    "tools\test_r1_resume_daemon_logic_wsl.ps1"
)
foreach ($file in $psFiles) {
    if (!(Test-Path -LiteralPath $file)) {
        throw "Missing PowerShell file: $file"
    }
    $errors = $null
    $null = [System.Management.Automation.PSParser]::Tokenize((Get-Content -Raw -Path $file), [ref]$errors)
    if ($errors -and $errors.Count -gt 0) {
        throw "PowerShell parse errors in $file`: $($errors | Out-String)"
    }
    Write-Host "OK   $file"
}

Invoke-Checked {
    $shellFiles = @(
        "tools\r1_audiobook_resume_daemon.sh",
        "tools\r1_audiobook_db_watch.sh",
        "tools\r1_audiobook_refresh.sh",
        "tools\test_r1_resume_daemon_logic.sh",
        "tools\test_r1_db_watch_logic.sh",
        "tools\test_r1_usb_adb_fallback.sh",
        "firmware\scripts\r1_usb_gadget_common.sh",
        "firmware\scripts\adbon",
        "firmware\scripts\adboff",
        "firmware\scripts\S90adb"
    )
    foreach ($file in $shellFiles) {
        $normalized = (Get-Content -Raw -LiteralPath $file).Replace("`r", "")
        $normalized | wsl -d $Distro --exec sh -n
        if ($LASTEXITCODE -ne 0) {
            throw "Shell syntax failed: $file"
        }
    }
} "Shell syntax"

Invoke-Checked {
    Invoke-Python -m py_compile `
        tools\verify_r1_audiobook_build.py `
        tools\write_audiobook_resume_catalog.py `
        tools\check_audiobook_release_state.py `
        tools\compare_binary_settings.py `
        tools\test_r1_db_maint_local_fixture.py `
        tools\adb_test_audiobook_launcher_route_variant.py `
        tools\adb_test_audiobook_launcher_callback.py `
        tools\adb_test_audiobook_launcher_record.py `
        tools\adb_test_audiobook_route_table_direct.py `
        tools\adb_test_audiobook_route_table_matrix.py `
        tools\adb_probe_route_callback.py `
        tools\adb_test_audiobook_ui_seek_fallback.py `
        tools\adb_test_audiobook_seek_restore.py `
        tools\adb_probe_music_row.py `
        tools\adb_probe_native_audiobook_hub.py `
        tools\adb_send_dmr_command.py `
        tools\generate_audiobook_m3u_views.py `
        tools\r1_adb_control.py `
        tools\r1_audiobook_ui_route_lab.py `
        tools\r1_hiby_player_cave_audit.py `
        tools\r1_hiby_player_listview_descriptor_report.py `
        tools\r1_hiby_player_ui_callsite_report.py `
        tools\r1_hiby_player_static_xrefs.py `
        tools\generate_audiobook_launcher_icons.py `
        tools\test_mp3_chapters.py `
        tools\test_music_catalog_cleanup.py
} "Python compile"

Invoke-Checked {
    $zig = Get-ChildItem ".deps\zig" -Recurse -Filter "zig.exe" |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $zig) {
        throw "Pinned Zig compiler not found under .deps\zig"
    }
    New-Item -ItemType Directory -Force "work\native-tests" | Out-Null
    & $zig cc -target x86_64-linux-musl -std=gnu99 `
        -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 `
        -Iaudiobook_app audiobook_app\tags.c audiobook_app\tags_probe.c `
        -o work\native-tests\tags_probe
    if ($LASTEXITCODE -ne 0) {
        throw "Host tags probe compilation failed with exit code $LASTEXITCODE"
    }
    wsl -d $Distro --cd $repoRoot --exec python3 `
        tools/test_mp3_chapters.py --probe work/native-tests/tags_probe
} "MP3 chapter parser fixtures"

Invoke-Checked {
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_music_catalog_cleanup.ps1
} "Native Music catalog cleanup fixtures"

Invoke-Checked {
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_r1_resume_daemon_logic_wsl.ps1 -Distro $Distro
} "Resume daemon logic"

Invoke-Checked {
    wsl -d $Distro --cd $repoRoot --exec sh tools/test_r1_db_watch_logic.sh
} "DB watcher logic"

if (-not $SkipDbFixtures) {
    if (Test-Path -LiteralPath $WindowsHelper) {
        Invoke-Checked {
            Invoke-Python tools\test_r1_db_maint_local_fixture.py --helper $WindowsHelper
        } "Windows DB helper fixture"
    } else {
        Write-Host "SKIP Windows DB helper fixture; missing $WindowsHelper"
    }

    if (Test-Path -LiteralPath $MipsHelper) {
        Invoke-Checked {
            powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_r1_db_maint_qemu_wsl.ps1 -Distro $Distro -Helper $MipsHelper
        } "QEMU MIPS DB helper fixture"
    } else {
        Write-Host "SKIP QEMU MIPS DB helper fixture; missing $MipsHelper"
    }
}

Invoke-Checked {
    git diff --check
} "Git diff whitespace"

Write-Host ""
Write-Host "All local sanity checks passed."
