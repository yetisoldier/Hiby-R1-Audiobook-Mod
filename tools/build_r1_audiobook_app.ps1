param(
    [Parameter(Mandatory=$false)]
    [string]$OutFile = "work\native-app\r1_audiobook_smoke",

    [Parameter(Mandatory=$false)]
    [string]$ZigVersion = "0.16.0",

    [Parameter(Mandatory=$false)]
    [switch]$SmokeOnly
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$depsDir = Join-Path $repoRoot ".deps"
$zigDir = Join-Path $depsDir "zig"
$zigExtractedDir = Join-Path $zigDir "zig-x86_64-windows-$ZigVersion"
$zigExe = Join-Path $zigExtractedDir "zig.exe"
$kernelInclude = Join-Path $zigExtractedDir "lib\libc\include\any-linux-any"

if (!(Test-Path -LiteralPath $zigExe)) {
    throw "Missing Zig compiler: $zigExe. Run tools\build_r1_db_maint_helper.ps1 once to install dependencies."
}

$outPath = if ([System.IO.Path]::IsPathRooted($OutFile)) {
    $OutFile
} else {
    Join-Path $repoRoot $OutFile
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null

# Smoke build: single C file, no SQLite, links libm for sin/lrint.
if ($SmokeOnly -or $true) {
    $source = Join-Path $repoRoot "audiobook_app\smoke.c"
    if (!(Test-Path -LiteralPath $source)) {
        throw "smoke source not found: $source"
    }

    & $zigExe cc `
        -target mipsel-linux-musleabi `
        -static `
        -Os `
        -s `
        -I $kernelInclude `
        -lm `
        $source `
        -o $outPath

    if ($LASTEXITCODE -ne 0) {
        throw "failed to build smoke $outPath"
    }
}

$built = Get-Item -LiteralPath $outPath
Write-Host ("Built: {0} ({1} bytes)" -f $built.FullName, $built.Length)
$blessed = Join-Path $repoRoot "work\native-app\r1_audiobook_smoke"
if ($built.FullName -ne $blessed) {
    Copy-Item -LiteralPath $built.FullName -Destination $blessed -Force
}
$blessedItem = Get-Item -LiteralPath $blessed
$blessedItem | Select-Object FullName, Length, LastWriteTime | Format-List