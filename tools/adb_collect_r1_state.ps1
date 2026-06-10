param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "C:\Program Files\Software Fix\adb.exe",

    [Parameter(Mandatory=$false)]
    [string]$OutDir = "device-dump"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path -LiteralPath $Adb)) {
    throw "ADB not found at $Adb"
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$target = Join-Path (Join-Path (Get-Location).Path $OutDir) $stamp
New-Item -ItemType Directory -Force -Path $target | Out-Null

function Pull-If-Present([string]$Remote, [string]$LocalName) {
    $probe = (& $Adb shell "if [ -e '$Remote' ]; then echo present; else echo missing; fi" 2>$null) -join "`n"
    if ($probe -match "present") {
        & $Adb pull $Remote (Join-Path $target $LocalName)
    }
    else {
        Write-Host "Skipping missing path: $Remote"
    }
}

& $Adb devices -l | Tee-Object -FilePath (Join-Path $target "adb-devices.txt")

& $Adb shell "uname -a; mount; ls -la /usr/data /data /data/mnt /data/mnt/sd_0 2>/dev/null" |
    Tee-Object -FilePath (Join-Path $target "device-state.txt")

Pull-If-Present "/usr/data/usrlocal_media.db" "usrlocal_media.db"
Pull-If-Present "/data/usrlocal_media.db" "data-usrlocal_media.db"
Pull-If-Present "/usr/data/book.db" "book.db"
Pull-If-Present "/data/book.db" "data-book.db"
Pull-If-Present "/usr/data/user.ini" "user.ini"
Pull-If-Present "/usr/data/momery_list.lst" "momery_list.lst"
Pull-If-Present "/usr/data/menu_cfg" "menu_cfg"

Write-Host "Saved device state to $target"
