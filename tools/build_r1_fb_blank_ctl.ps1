param(
    [Parameter(Mandatory=$false)]
    [string]$OutFile = "work\fb-blank-test\r1_fb_blank_ctl",

    [Parameter(Mandatory=$false)]
    [string]$ZigVersion = "0.16.0"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$zigExe = Join-Path $repoRoot ".deps\zig\zig-x86_64-windows-$ZigVersion\zig.exe"
$source = Join-Path $repoRoot "tools\r1_fb_blank_ctl.c"
$outPath = if ([System.IO.Path]::IsPathRooted($OutFile)) {
    $OutFile
} else {
    Join-Path $repoRoot $OutFile
}

if (!(Test-Path -LiteralPath $zigExe)) {
    throw "Missing Zig compiler: $zigExe. Build the audiobook hook once to install dependencies."
}
if (!(Test-Path -LiteralPath $source)) {
    throw "Missing source: $source"
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null

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
