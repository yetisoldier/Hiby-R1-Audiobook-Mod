param(
    [Parameter(Mandatory=$false)]
    [string[]]$Variants = @(
        "callback-simple-only",
        "next-empty-only",
        "next-empty-simple",
        "songs-of-genre-simple"
    ),

    [Parameter(Mandatory=$false)]
    [string]$OutputRoot = "work\route-matrix-live",

    [Parameter(Mandatory=$false)]
    [double]$OpenDelaySeconds = 6.0,

    [Parameter(Mandatory=$false)]
    [double]$BackDelaySeconds = 1.5,

    [Parameter(Mandatory=$false)]
    [int]$ReturnBackCount = 4,

    [Parameter(Mandatory=$false)]
    [int]$TouchFrames = 8,

    [Parameter(Mandatory=$false)]
    [switch]$SkipBackCapture,

    [Parameter(Mandatory=$false)]
    [switch]$SkipCurrentBaseline,

    [Parameter(Mandatory=$false)]
    [switch]$SkipReturnToLauncher
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outDir = Join-Path $OutputRoot $stamp
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$summaryPath = Join-Path $outDir "summary.tsv"
"variant`tphase`tresult" | Set-Content -LiteralPath $summaryPath -Encoding ASCII

$Variants = @(
    foreach ($variant in $Variants) {
        foreach ($part in ($variant -split ",")) {
            $trimmed = $part.Trim()
            if ($trimmed) {
                $trimmed
            }
        }
    }
)

function Invoke-Checked {
    param(
        [scriptblock]$Command,
        [string]$Label
    )
    & $Command | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Add-Summary {
    param(
        [string]$Variant,
        [string]$Phase,
        [string]$Result
    )
    "$Variant`t$Phase`t$Result" | Add-Content -LiteralPath $summaryPath -Encoding ASCII
}

function Invoke-Capture {
    param(
        [string]$Label
    )
    $safe = ($Label -replace '[^A-Za-z0-9_.-]', '_')
    $png = Join-Path $outDir "$safe.png"
    $raw = Join-Path $outDir "$safe.raw"
    Invoke-Checked {
        python tools\r1_adb_control.py screenshot `
            --output $png `
            --raw-output $raw `
            --label $safe
    } "capture $Label"
    return $png
}

function Invoke-EdgeBack {
    param(
        [string]$Label
    )
    Invoke-Checked {
        python tools\r1_adb_control.py macro edge-back `
            --frames $TouchFrames `
            --settle $BackDelaySeconds
    } "edge back $Label"
}

function Invoke-ReturnTowardLauncher {
    param(
        [string]$Variant
    )
    if ($SkipReturnToLauncher) {
        return
    }
    Add-Summary $Variant "return-start" "edge-back count=$ReturnBackCount"
    for ($i = 1; $i -le $ReturnBackCount; $i++) {
        Invoke-EdgeBack "$Variant-$i"
    }
    $capture = Invoke-Capture "$Variant-returned"
    Add-Summary $Variant "return-capture" $capture
}

Write-Host "output: $outDir"
Invoke-Checked { python tools\adb_test_audiobook_route_table_matrix.py } "initial route state"
Invoke-Capture "initial-screen" | Out-Null

if (-not $SkipCurrentBaseline) {
    Write-Host ""
    Write-Host "== current-baseline =="
    Invoke-ReturnTowardLauncher "current-baseline"
    Invoke-Checked {
        python tools\r1_adb_control.py preset main-audiobooks `
            --frames $TouchFrames
    } "open Audiobooks for current baseline"
    Start-Sleep -Seconds $OpenDelaySeconds
    $baselineCapture = Invoke-Capture "current-baseline-open-result"
    Add-Summary "current-baseline" "open" $baselineCapture
    if (-not $SkipBackCapture) {
        Invoke-EdgeBack "current-baseline-back"
        $baselineBackCapture = Invoke-Capture "current-baseline-back-result"
        Add-Summary "current-baseline" "back" $baselineBackCapture
    }
}

foreach ($variant in $Variants) {
    Write-Host ""
    Write-Host "== $variant =="
    try {
        Invoke-ReturnTowardLauncher $variant
        Invoke-Checked {
            python tools\adb_test_audiobook_route_table_matrix.py `
                --variant $variant `
                --apply `
                --i-understand-this-writes-process-memory
        } "apply $variant"
        Add-Summary $variant "apply" "ok"

        Invoke-Checked {
            python tools\r1_adb_control.py preset main-audiobooks `
                --frames $TouchFrames
        } "open Audiobooks for $variant"
        Start-Sleep -Seconds $OpenDelaySeconds
        $openCapture = Invoke-Capture "$variant-open-result"
        Add-Summary $variant "open" $openCapture

        if (-not $SkipBackCapture) {
            Invoke-EdgeBack "$variant-back"
            $backCapture = Invoke-Capture "$variant-back-result"
            Add-Summary $variant "back" $backCapture
        }
    } catch {
        Add-Summary $variant "error" $_.Exception.Message
        throw
    } finally {
        python tools\adb_test_audiobook_route_table_matrix.py `
            --revert `
            --i-understand-this-writes-process-memory
        if ($LASTEXITCODE -ne 0) {
            throw "failed to revert route matrix after $variant"
        }
        Add-Summary $variant "revert" "ok"
    }
}

Invoke-Checked { python tools\adb_test_audiobook_route_table_matrix.py } "final route state"
Write-Host "summary: $summaryPath"
