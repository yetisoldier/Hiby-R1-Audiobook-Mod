param(
    [Parameter(Mandatory=$false)]
    [switch]$SkipDbFixtures,

    [Parameter(Mandatory=$false)]
    [string]$Distro = "Ubuntu-24.04",

    [Parameter(Mandatory=$false)]
    [string]$WindowsHelper = "work\native-db-maint\r1_audiobook_db_maint_win_test.exe",

    [Parameter(Mandatory=$false)]
    [string]$MipsHelper = "work\native-db-maint\r1_audiobook_db_maint_enhanced"
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

Step "PowerShell parser"
$psFiles = @(
    "tools\adb_archive_audiobook_dev_artifacts.ps1",
    "tools\adb_build_release_audiobook_db.ps1",
    "tools\adb_collect_audiobook_resume_debug.ps1",
    "tools\adb_collect_r1_state.ps1",
    "tools\adb_install_audiobook_resume_runtime.ps1",
    "tools\adb_install_release_audiobook_db.ps1",
    "tools\adb_manage_boot_adb.ps1",
    "tools\adb_monitor_r1_runtime.ps1",
    "tools\adb_snapshot_r1_settings.ps1",
    "tools\adb_stage_verified_firmware.ps1",
    "tools\adb_verify_installed_audiobook_release.ps1",
    "tools\build_r1_audiobook_firmware.ps1",
    "tools\build_r1_db_maint_helper.ps1",
    "tools\extract_r1_firmware.ps1",
    "tools\test_r1_db_maint_qemu_wsl.ps1",
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
    wsl -d $Distro --cd $repoRoot --exec sh -lc 'sh -n tools/r1_audiobook_resume_daemon.sh && sh -n tools/r1_audiobook_db_watch.sh && sh -n tools/test_r1_resume_daemon_logic.sh'
} "Shell syntax"

Invoke-Checked {
    python -m py_compile `
        tools\verify_r1_audiobook_build.py `
        tools\write_audiobook_resume_catalog.py `
        tools\check_audiobook_release_state.py `
        tools\test_r1_db_maint_local_fixture.py `
        tools\adb_test_audiobook_launcher_route_variant.py `
        tools\adb_test_audiobook_ui_seek_fallback.py `
        tools\adb_test_audiobook_seek_restore.py `
        tools\adb_send_dmr_command.py `
        tools\r1_adb_control.py
} "Python compile"

Invoke-Checked {
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_r1_resume_daemon_logic_wsl.ps1 -Distro $Distro
} "Resume daemon logic"

if (-not $SkipDbFixtures) {
    if (Test-Path -LiteralPath $WindowsHelper) {
        Invoke-Checked {
            python tools\test_r1_db_maint_local_fixture.py --helper $WindowsHelper
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
