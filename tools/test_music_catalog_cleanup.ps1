param(
    [Parameter(Mandatory=$false)]
    [string]$OutFile = "work\native-tests\music_catalog_cleanup_test.exe"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$zig = Get-ChildItem ".deps\zig" -Recurse -Filter "zig.exe" |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $zig) {
    throw "Pinned Zig compiler not found under .deps\zig"
}

New-Item -ItemType Directory -Force (Split-Path -Parent $OutFile) | Out-Null
& $zig cc `
    -target x86_64-windows-gnu `
    -Os `
    -I audiobook_app `
    -DSQLITE_THREADSAFE=2 `
    -DSQLITE_DEFAULT_MEMSTATUS=0 `
    -DSQLITE_OMIT_LOAD_EXTENSION=1 `
    -DSQLITE_OMIT_DEPRECATED=1 `
    -DSQLITE_TEMP_STORE=2 `
    audiobook_app\music_catalog.c `
    tools\music_catalog_cleanup_test_main.c `
    audiobook_app\sqlite3.c `
    -o $OutFile
if ($LASTEXITCODE -ne 0) {
    throw "Music catalog cleanup test build failed"
}

python tools\test_music_catalog_cleanup.py --helper $OutFile
if ($LASTEXITCODE -ne 0) {
    throw "Synthetic Music catalog cleanup fixture failed"
}

$capturedFixtures = @(
    "work\db-audiobooks-all-20260609\usrlocal_media.with-audiobooks.db",
    "work\usrlocal_media.db"
)
foreach ($fixture in $capturedFixtures) {
    if (Test-Path -LiteralPath $fixture) {
        python tools\test_music_catalog_cleanup.py --helper $OutFile --fixture $fixture
        if ($LASTEXITCODE -ne 0) {
            throw "Music catalog cleanup fixture failed: $fixture"
        }
    } else {
        Write-Host "SKIP optional captured Music catalog fixture: $fixture"
    }
}
