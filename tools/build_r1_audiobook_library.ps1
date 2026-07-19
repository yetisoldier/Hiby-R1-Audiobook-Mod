param(
    [Parameter(Mandatory=$false)]
    [string]$OutFile = "work\native-app\r1_audiobook_library_test",

    [Parameter(Mandatory=$false)]
    [string]$ZigVersion = "0.16.0"
)

$ErrorActionPreference = "Stop"

# Resolve Python (WindowsApps stub may intercept)
$_pyOk = $true
try { $_pyVer = & python --version 2>&1; if ($LASTEXITCODE -ne 0) { $_pyOk = $false } } catch { $_pyOk = $false }
if (-not $_pyOk) {
    function python { & py -3 @args }
}

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

$sources = @(
    "audiobook_app\library_test.c",
    "audiobook_app\library.c",
    "audiobook_app\scan.c",
    "audiobook_app\tags.c",
    "audiobook_app\sqlite3.c"
)

foreach ($src in $sources) {
    $fullSrc = Join-Path $repoRoot $src
    if (!(Test-Path -LiteralPath $fullSrc)) {
        throw "Source not found: $fullSrc"
    }
}

# Build as a static MIPS ELF for the device (or host-native for testing).
# SQLite needs: SQLITE_THREADSAFE=0 (single-threaded, smaller)
#              SQLITE_DEFAULT_MEMSTATUS=0 (no memory stats)
#              SQLITE_OMIT_LOAD_EXTENSION (no dynamic extensions)
#              SQLITE_ENABLE_FTS5 (FTS5 search)
$sqliteDefines = @(
    "-DSQLITE_THREADSAFE=0",
    "-DSQLITE_DEFAULT_MEMSTATUS=0",
    "-DSQLITE_OMIT_LOAD_EXTENSION=1",
    "-DSQLITE_ENABLE_FTS5=1",
    "-DSQLITE_OMIT_DEPRECATED=1",
    "-DSQLITE_TEMP_STORE=2"
)

Write-Host "Building library test binary..."
Write-Host "  Target: mipsel-linux-musleabi (device, static)"
Write-Host "  Output: $outPath"
Write-Host ""

& $zigExe cc `
    -target mipsel-linux-musleabi `
    -static `
    -Os `
    -s `
    -I $kernelInclude `
    $sqliteDefines `
    $sources `
    -o $outPath

if ($LASTEXITCODE -ne 0) {
    throw "failed to build library test binary"
}

$builtFile = Get-Item -LiteralPath $outPath
Write-Host ""
Write-Host ("Built: {0} ({1} bytes)" -f $builtFile.FullName, $builtFile.Length)
Write-Host ""
return $builtFile