param(
    [Parameter(Mandatory=$false)]
    [string]$Distro = "Ubuntu-24.04",

    [Parameter(Mandatory=$false)]
    [string]$ObjdumpRoot = ".deps\mips-binutils\root",

    [Parameter(Mandatory=$true)]
    [string]$Binary,

    [Parameter(Mandatory=$false)]
    [string]$StartAddress,

    [Parameter(Mandatory=$false)]
    [string]$StopAddress,

    [Parameter(Mandatory=$false)]
    [string[]]$ExtraArgs = @("-D")
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Convert-ToWslPath([string]$PathValue) {
    $resolved = Resolve-PathStrict $PathValue
    $converted = wsl -d $Distro --exec wslpath -a "$resolved"
    if ($LASTEXITCODE -ne 0) {
        throw "wslpath failed for $PathValue"
    }
    return ($converted -join "").Trim()
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$objdumpRootPath = Resolve-PathStrict $ObjdumpRoot
$objdumpRootWsl = Convert-ToWslPath $objdumpRootPath
$binaryWsl = Convert-ToWslPath $Binary

$argsList = @()
$argsList += $ExtraArgs
if ($StartAddress) {
    $argsList += "--start-address=$StartAddress"
}
if ($StopAddress) {
    $argsList += "--stop-address=$StopAddress"
}
$argsList += $binaryWsl

$quotedArgs = ($argsList | ForEach-Object { "'" + ($_ -replace "'", "'\''") + "'" }) -join " "
$command = "LD_LIBRARY_PATH='$objdumpRootWsl/usr/lib/x86_64-linux-gnu' '$objdumpRootWsl/usr/bin/mipsel-linux-gnu-objdump' $quotedArgs"

wsl -d $Distro --exec sh -lc $command
if ($LASTEXITCODE -ne 0) {
    throw "mips objdump failed"
}
