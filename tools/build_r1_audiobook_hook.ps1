param(
    [Parameter(Mandatory=$false)]
    [string]$OutFile = "work\native-app\libaudiobook_hook.so",

    [Parameter(Mandatory=$false)]
    [string]$ZigVersion = "0.16.0"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$depsDir = Join-Path $repoRoot ".deps"
$zigDir = Join-Path $depsDir "zig"
$zigExtractedDir = Join-Path $zigDir "zig-x86_64-windows-$ZigVersion"
$zigExe = Join-Path $zigExtractedDir "zig.exe"
$kernelInclude = Join-Path $zigExtractedDir "lib\libc\include\any-linux-any"
$vendorInclude = Join-Path $repoRoot "vendor"   # minimp3_ex.h / minimp3.h (MP3 decoder)
$libjpegInclude = Join-Path $repoRoot "vendor\libjpeg"   # jpeglib.h (structs for dlopen'd libjpeg)

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
    "audiobook_app\hook.c",
    "audiobook_app\ui.c",
    "audiobook_app\render.c",
    "audiobook_app\font.c",
    "audiobook_app\library.c",
    "audiobook_app\scan.c",
    "audiobook_app\tags.c",
    "audiobook_app\mp4_audio.c",
    "audiobook_app\cover.c",
    "audiobook_app\player.c",
    "audiobook_app\wsola.c",
    "audiobook_app\sqlite3.c"
)

foreach ($src in $sources) {
    $fullSrc = Join-Path $repoRoot $src
    if (!(Test-Path -LiteralPath $fullSrc)) {
        throw "Source not found: $fullSrc"
    }
}

# SQLite compile options (smaller, single-threaded, FTS5 enabled)
$sqliteDefines = @(
    "-DSQLITE_THREADSAFE=0",
    "-DSQLITE_DEFAULT_MEMSTATUS=0",
    "-DSQLITE_OMIT_LOAD_EXTENSION=1",
    "-DSQLITE_ENABLE_FTS5=1",
    "-DSQLITE_OMIT_DEPRECATED=1",
    "-DSQLITE_TEMP_STORE=2"
)

# Build as a shared library (PIC, dynamic) for LD_PRELOAD.
# Must use glibc + hard-float (gnueabihf) — hiby_player is glibc-linked
# with hard-float (double precision). ld.so refuses to load a soft-float
# .so into a hard-float process ("uses Soft float, already loaded Hard
# float"). Device has glibc 2.22.
# Target suffix .2.22: pins to glibc 2.22 ABI so the .so doesn't use
#   symbols from newer glibc (Zig ships glibc 2.33+ by default, which
#   causes "GLIBC_2.28 not found" on the device's glibc 2.22).
# -fvisibility=hidden: hides all symbols (sqlite3_*, audiobook_*, etc.)
#   so they don't shadow hiby_player's own SQLite. Constructors are
#   found via .init_array, not symbol lookup.
# -shared -fPIC: position-independent shared object
# -Os -s: optimize for size, strip symbols
& $zigExe cc `
    -target mipsel-linux-gnueabihf.2.22 `
    -shared `
    -fPIC `
    -fvisibility=hidden `
    -fno-common `
    -Os `
    -s `
    -I $kernelInclude `
    -I $vendorInclude `
    -I $libjpegInclude `
    $sqliteDefines `
    $sources `
    -lpthread -ldl -lm `
    -o $outPath

if ($LASTEXITCODE -ne 0) {
    throw "failed to build hook library $outPath"
}

$builtFile = Get-Item -LiteralPath $outPath
Write-Host ""
Write-Host ("Built: {0} ({1} bytes)" -f $builtFile.FullName, $builtFile.Length)
Write-Host ""
return $builtFile