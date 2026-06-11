param(
    [Parameter(Mandatory=$false)]
    [string]$Distro = "Ubuntu-24.04",

    [Parameter(Mandatory=$false)]
    [string]$DestDir = ".deps\mips-binutils",

    [Parameter(Mandatory=$false)]
    [string]$Package = "binutils-mipsel-linux-gnu"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$destUnix = $DestDir -replace "\\", "/"
$script = @"
set -eu
mkdir -p '$destUnix/download' '$destUnix/root'
cd '$destUnix/download'
if ! ls ${Package}_*.deb >/dev/null 2>&1; then
    apt-get download '$Package'
fi
dpkg-deb -x ${Package}_*.deb ../root
LD_LIBRARY_PATH=../root/usr/lib/x86_64-linux-gnu ../root/usr/bin/mipsel-linux-gnu-objdump --version | head -2
"@

wsl -d $Distro --cd $repoRoot --exec sh -lc $script
if ($LASTEXITCODE -ne 0) {
    throw "failed to install local MIPS binutils under $DestDir"
}

Write-Host "Local MIPS binutils are available under $DestDir"
