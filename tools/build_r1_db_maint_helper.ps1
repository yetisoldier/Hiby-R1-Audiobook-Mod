param(
    [Parameter(Mandatory=$false)]
    [string]$OutFile = "work\native-db-maint\r1_audiobook_db_maint",

    [Parameter(Mandatory=$false)]
    [string]$ZigVersion = "0.16.0",

    [Parameter(Mandatory=$false)]
    [string]$SqliteVersion = "3530200"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$depsDir = Join-Path $repoRoot ".deps"
$zigDir = Join-Path $depsDir "zig"
$sqliteDir = Join-Path $depsDir "sqlite"

$zigZipName = "zig-x86_64-windows-$ZigVersion.zip"
$zigZip = Join-Path $zigDir $zigZipName
$zigExtractedDir = Join-Path $zigDir "zig-x86_64-windows-$ZigVersion"
$zigExe = Join-Path $zigExtractedDir "zig.exe"
$zigUrl = "https://ziglang.org/download/$ZigVersion/$zigZipName"
$zigSha256 = "68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e"

$sqliteZipName = "sqlite-amalgamation-$SqliteVersion.zip"
$sqliteZip = Join-Path $sqliteDir $sqliteZipName
$sqliteExtractedDir = Join-Path $sqliteDir "sqlite-amalgamation-$SqliteVersion"
$sqliteUrl = "https://sqlite.org/2026/$sqliteZipName"
$sqliteSha3_256 = "81142986038e18f96c4a54e1a72562ae17e502a916f2a7701eff43388cbf1a40"

function Require-Tool([string]$Command) {
    if (-not (Get-Command $Command -ErrorAction SilentlyContinue)) {
        throw "Required tool not found on PATH: $Command"
    }
}

function Download-IfMissing([string]$Url, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) {
        return
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Invoke-WebRequest -Uri $Url -OutFile $Destination
}

function Assert-Sha256([string]$Path, [string]$Expected) {
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) {
        throw "SHA256 mismatch for $Path. Expected $Expected, got $actual"
    }
}

function Assert-Sha3_256([string]$Path, [string]$Expected) {
    $script = @"
import hashlib
from pathlib import Path
path = Path(r'''$Path''')
print(hashlib.sha3_256(path.read_bytes()).hexdigest())
"@
    $actual = (& python -c $script).Trim().ToLowerInvariant()
    if ($LASTEXITCODE -ne 0) {
        throw "python failed while hashing $Path"
    }
    if ($actual -ne $Expected) {
        throw "SHA3-256 mismatch for $Path. Expected $Expected, got $actual"
    }
}

Require-Tool "python"
Require-Tool "tar.exe"

Download-IfMissing $zigUrl $zigZip
Assert-Sha256 $zigZip $zigSha256
if (-not (Test-Path -LiteralPath $zigExe)) {
    tar.exe -xf $zigZip -C $zigDir
}

Download-IfMissing $sqliteUrl $sqliteZip
Assert-Sha3_256 $sqliteZip $sqliteSha3_256
if (-not (Test-Path -LiteralPath (Join-Path $sqliteExtractedDir "sqlite3.c"))) {
    tar.exe -xf $sqliteZip -C $sqliteDir
}

$outPath = if ([System.IO.Path]::IsPathRooted($OutFile)) {
    $OutFile
} else {
    Join-Path $repoRoot $OutFile
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $outPath) | Out-Null

$source = Join-Path $repoRoot "tools\r1_audiobook_db_maint.c"
$sqliteSource = Join-Path $sqliteExtractedDir "sqlite3.c"

& $zigExe cc `
    -target mipsel-linux-musleabi `
    -static `
    -Os `
    -s `
    -I $sqliteExtractedDir `
    -DSQLITE_THREADSAFE=0 `
    -DSQLITE_OMIT_LOAD_EXTENSION `
    -DSQLITE_DEFAULT_MEMSTATUS=0 `
    -DSQLITE_OMIT_DEPRECATED `
    $source `
    $sqliteSource `
    -o $outPath

if ($LASTEXITCODE -ne 0) {
    throw "failed to build $outPath"
}

Get-Item -LiteralPath $outPath
