param(
    [Parameter(Mandatory=$false)]
    [string]$OutFile = "work\native-direct-open\r1_audiobook_direct_open",

    [Parameter(Mandatory=$false)]
    [string]$ZigVersion = "0.16.0"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$depsDir = Join-Path $repoRoot ".deps"
$zigDir = Join-Path $depsDir "zig"
$zigExtractedDir = Join-Path $zigDir "zig-x86_64-windows-$ZigVersion"
$zigExe = Join-Path $zigExtractedDir "zig.exe"

if (!(Test-Path -LiteralPath $zigExe)) {
    throw "Missing Zig compiler: $zigExe. Run tools\build_r1_db_maint_helper.ps1 once to install dependencies."
}

$outPath = if ([System.IO.Path]::IsPathRooted($OutFile)) {
    $OutFile
} else {
    Join-Path $repoRoot $OutFile
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null

$source = Join-Path $repoRoot "tools\r1_audiobook_direct_open.c"

& $zigExe cc `
    -target mipsel-linux-musleabi `
    -static `
    -Os `
    -s `
    $source `
    -o $outPath

if ($LASTEXITCODE -ne 0) {
    throw "failed to build $outPath"
}

Get-Item -LiteralPath $outPath
