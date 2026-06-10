param(
    [Parameter(Mandatory=$false)]
    [string]$UptPath = "stock\r1.upt",

    [Parameter(Mandatory=$false)]
    [string]$OutDir = "work\original",

    [Parameter(Mandatory=$false)]
    [string]$SevenZip = "C:\Program Files\7-Zip\7z.exe"
)

$ErrorActionPreference = "Stop"

function Resolve-WorkspacePath([string]$PathValue) {
    if ([IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }
    return Join-Path (Get-Location).Path $PathValue
}

function Join-Chunks([string]$InputDir, [string]$Pattern, [string]$OutputPath) {
    Remove-Item -LiteralPath $OutputPath -ErrorAction SilentlyContinue
    $stream = [IO.File]::Open($OutputPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
    try {
        Get-ChildItem -LiteralPath $InputDir -File |
            Where-Object { $_.Name -match $Pattern } |
            Sort-Object Name |
            ForEach-Object {
                $bytes = [IO.File]::ReadAllBytes($_.FullName)
                $stream.Write($bytes, 0, $bytes.Length)
            }
    }
    finally {
        $stream.Close()
    }
}

$upt = Resolve-WorkspacePath $UptPath
$out = Resolve-WorkspacePath $OutDir
$extractDir = Join-Path $out "upt"

if (!(Test-Path -LiteralPath $SevenZip)) {
    throw "7-Zip not found at $SevenZip"
}
if (!(Test-Path -LiteralPath $upt)) {
    throw "Firmware package not found: $upt"
}

New-Item -ItemType Directory -Force -Path $out | Out-Null
Remove-Item -Recurse -Force -LiteralPath $extractDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $extractDir | Out-Null

& $SevenZip x $upt "-o$extractDir" -y | Out-Host

$ota = Join-Path $extractDir "ota_v0"
if (!(Test-Path -LiteralPath $ota)) {
    throw "Extracted package does not contain ota_v0"
}

Join-Chunks $ota '^rootfs\.squashfs\.\d{4}\.' (Join-Path $out "rootfs.squashfs")
Join-Chunks $ota '^xImage\.\d{4}\.' (Join-Path $out "xImage")

Get-Item (Join-Path $out "rootfs.squashfs"), (Join-Path $out "xImage") |
    Select-Object FullName, Length |
    Format-Table -AutoSize

