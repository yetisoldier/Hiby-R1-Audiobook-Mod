param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [string]$OutDir = "work\release-db-candidate",

    [Parameter(Mandatory=$false)]
    [int]$IdBase = 1000,

    [Parameter(Mandatory=$false)]
    [string]$DeviceAudiobooksRoot = "/usr/data/mnt/sd_0/Audiobooks",

    [Parameter(Mandatory=$false)]
    [string]$SourceDbRemote = "/usr/data/usrlocal_media.db",

    [switch]$SkipAdbSizes
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

$adbPath = Resolve-PathStrict $Adb
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$target = Join-Path (Resolve-Path -LiteralPath (Get-Location).Path).Path (Join-Path $OutDir $stamp)
New-Item -ItemType Directory -Force -Path $target | Out-Null

$sourceDb = Join-Path $target "usrlocal_media.source.db"
$candidateDb = Join-Path $target "usrlocal_media.release-candidate.db"
$catalog = Join-Path $target "catalog.release-candidate.tsv"
$hashes = Join-Path $target "hashes.txt"

& $adbPath devices | Tee-Object -FilePath (Join-Path $target "adb-devices.txt") | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

& $adbPath pull $SourceDbRemote $sourceDb | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "failed to pull source media DB"
}

$addArgs = @(
    "tools\add_audiobooks_to_media_db.py",
    $sourceDb,
    "--adb-scan",
    "--adb",
    $adbPath,
    "--device-root",
    $DeviceAudiobooksRoot,
    "--music-catalog-excludes-audiobooks",
    "--id-base",
    "$IdBase",
    "-o",
    $candidateDb
)
if (!$SkipAdbSizes) {
    $addArgs += "--adb-sizes"
}

python @addArgs | Tee-Object -FilePath (Join-Path $target "build-db.log") | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "failed to build release candidate DB"
}

python tools\write_audiobook_resume_catalog.py $candidateDb -o $catalog | Tee-Object -FilePath (Join-Path $target "build-catalog.log") | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "failed to build release candidate catalog"
}

python tools\check_audiobook_release_state.py $candidateDb --catalog $catalog --expect-audiobooks | Tee-Object -FilePath (Join-Path $target "release-check.log") | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "release candidate DB failed release-state check"
}

@(
    "source_db=$sourceDb",
    "candidate_db=$candidateDb",
    "catalog=$catalog",
    "candidate_db_md5=$((Get-FileHash -Algorithm MD5 -LiteralPath $candidateDb).Hash.ToLowerInvariant())",
    "candidate_db_sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $candidateDb).Hash.ToLowerInvariant())",
    "catalog_md5=$((Get-FileHash -Algorithm MD5 -LiteralPath $catalog).Hash.ToLowerInvariant())",
    "catalog_sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $catalog).Hash.ToLowerInvariant())"
) | Set-Content -LiteralPath $hashes -Encoding ASCII

Get-Content -LiteralPath $hashes | Out-Host
Write-Host "Release DB candidate directory: $target"
