param(
    [Parameter(Mandatory=$false)]
    [string]$Distro = "Ubuntu-24.04",

    [Parameter(Mandatory=$false)]
    [string]$Helper = "work\native-db-maint\r1_audiobook_db_maint_enhanced"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot

function Invoke-WslChecked([string]$Command) {
    & wsl -d $Distro --cd $repoRoot --exec sh -lc $Command
    if ($LASTEXITCODE -ne 0) {
        throw "WSL command failed with exit code ${LASTEXITCODE}: $Command"
    }
}

function Get-RepoRelativePath([string]$Root, [string]$Path) {
    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $pathFull = [System.IO.Path]::GetFullPath($Path)
    if (-not $pathFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Helper must be inside the repository for WSL --cd testing: $Path"
    }
    return $pathFull.Substring($rootFull.Length)
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw "wsl.exe was not found. Install WSL before running this test."
}

try {
    Invoke-WslChecked "true"
} catch {
    throw "WSL distro '$Distro' is not available or cannot launch. Original error: $($_.Exception.Message)"
}

$helperPath = if ([System.IO.Path]::IsPathRooted($Helper)) {
    (Resolve-Path -LiteralPath $Helper).Path
} else {
    (Resolve-Path -LiteralPath (Join-Path $repoRoot $Helper)).Path
}

$relativeHelper = (Get-RepoRelativePath $repoRoot $helperPath).Replace('\', '/')

Invoke-WslChecked "command -v qemu-mipsel-static >/dev/null"
Invoke-WslChecked "qemu-mipsel-static '$relativeHelper' --help >/dev/null"
Invoke-WslChecked "python3 tools/test_r1_db_maint_local_fixture.py --helper '$relativeHelper' --runner qemu-mipsel-static"
