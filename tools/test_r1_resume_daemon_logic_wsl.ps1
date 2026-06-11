param(
    [Parameter(Mandatory=$false)]
    [string]$Distro = "Ubuntu-24.04"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw "wsl.exe was not found. Install WSL before running this test."
}

& wsl -d $Distro --cd $repoRoot --exec sh tools/test_r1_resume_daemon_logic.sh
if ($LASTEXITCODE -ne 0) {
    throw "Resume daemon logic test failed with exit code $LASTEXITCODE"
}
