param(
    [Parameter(Mandatory=$false)]
    [string]$Adb = "",

    [Parameter(Mandatory=$false)]
    [string]$ControlScript = "tools\r1_adb_control.py",

    [Parameter(Mandatory=$false)]
    [string]$OutDir = "work\live-audiobook-smoke",

    [Parameter(Mandatory=$false)]
    [int]$TitleRow = 1,

    [Parameter(Mandatory=$false)]
    [int]$AudiobooksSettleSeconds = 10,

    [Parameter(Mandatory=$false)]
    [int]$StartSettleSeconds = 12,

    [Parameter(Mandatory=$false)]
    [int]$PlaybackSeconds = 22,

    [Parameter(Mandatory=$false)]
    [int]$TapFrames = 36,

    [Parameter(Mandatory=$false)]
    [int]$AudiobooksOpenRetries = 2,

    [Parameter(Mandatory=$false)]
    [int]$ResetBacks = 0,

    [Parameter(Mandatory=$false)]
    [int]$BackSettleSeconds = 2,

    [switch]$SkipOpenAudiobooks,

    [switch]$SkipStartTitle,

    [switch]$SkipAudiobookTitleListCheck,

    [switch]$NoPauseAfter
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Resolve-AdbPath([string]$PathValue) {
    if ($PathValue -and (Test-Path -LiteralPath $PathValue)) {
        return (Resolve-Path -LiteralPath $PathValue).Path
    }
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $repoAdb = Join-Path $repoRoot ".tools\platform-tools\adb.exe"
    if (Test-Path -LiteralPath $repoAdb) {
        return (Resolve-Path -LiteralPath $repoAdb).Path
    }
    $pathAdb = Get-Command adb -ErrorAction SilentlyContinue
    if ($pathAdb) {
        return $pathAdb.Source
    }
    throw "ADB not found. Install platform-tools, add adb to PATH, or place adb.exe at .tools\platform-tools\adb.exe."
}

function Invoke-AdbText([string]$Command) {
    $output = & $adbPath shell $Command
    if ($LASTEXITCODE -ne 0) {
        throw "adb shell failed: $Command"
    }
    return ($output -join "`n")
}

function Get-RemoteByteCount([string]$RemotePath) {
    $text = Invoke-AdbText "wc -c < '$RemotePath' 2>/dev/null || echo 0"
    if ($text -match "([0-9]+)") {
        return [int64]$Matches[1]
    }
    return [int64]0
}

function Invoke-Control([string[]]$ControlArgs) {
    $output = & python $controlScriptPath @ControlArgs
    if ($LASTEXITCODE -ne 0) {
        throw "r1_adb_control.py failed: $($ControlArgs -join ' ')"
    }
    return ($output -join "`n")
}

function Capture-SmokeScreen([string]$Name, [string]$ExpectedState = "") {
    $path = Join-Path $smokeDir "$Name.png"
    $output = Invoke-Control -ControlArgs @("screenshot", "--adb", $adbPath, "--output", $path, "--label", $Name, "--classify")
    Set-Content -LiteralPath (Join-Path $smokeDir "$Name.capture.txt") -Value $output
    $stateLine = ($output -split "`n" | Where-Object { $_ -match "^state:\s+" } | Select-Object -First 1)
    $state = ""
    if ($stateLine -and $stateLine -match "^state:\s+(.+)$") {
        $state = $Matches[1].Trim()
    }
    if ($ExpectedState -and $state -ne $ExpectedState) {
        throw "$Name expected screen state [$ExpectedState], got [$state]"
    }
    Write-Host "OK   captured $Name"
    if ($state) {
        Write-Host "OK   $Name screen state: $state"
    }
    return $state
}

function Get-SmokeRawPath([string]$Name) {
    return (Join-Path $smokeDir "$Name.raw")
}

function Get-Rgb565WhitePixelsRegion(
    [string]$RawPath,
    [int]$X0,
    [int]$Y0,
    [int]$X1,
    [int]$Y1,
    [int]$Step = 1
) {
    if (!(Test-Path -LiteralPath $RawPath)) {
        throw "Missing raw framebuffer capture: $RawPath"
    }
    $width = 480
    $stride = $width * 2
    $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $RawPath).Path)
    $count = 0
    for ($y = [Math]::Max(0, $Y0); $y -lt [Math]::Min(800, $Y1); $y += [Math]::Max(1, $Step)) {
        $row = $y * $stride
        for ($x = [Math]::Max(0, $X0); $x -lt [Math]::Min($width, $X1); $x += [Math]::Max(1, $Step)) {
            $offset = $row + ($x * 2)
            if ($offset + 1 -ge $bytes.Length) {
                continue
            }
            $value = [int]$bytes[$offset] -bor ([int]$bytes[$offset + 1] -shl 8)
            $r = ($value -shr 11) -band 0x1f
            $g = ($value -shr 5) -band 0x3f
            $b = $value -band 0x1f
            if ($r -ge 24 -and $g -ge 48 -and $b -ge 24) {
                $count++
            }
        }
    }
    return $count
}

function Test-AudiobookTitleListScreen([string]$RawPath) {
    $subheaderPixels = Get-Rgb565WhitePixelsRegion $RawPath 60 118 220 155 1
    $headerMidPixels = Get-Rgb565WhitePixelsRegion $RawPath 170 70 260 110 1
    $headerIconPixels = Get-Rgb565WhitePixelsRegion $RawPath 400 75 440 110 1
    $summary = "subheader=$subheaderPixels header_mid=$headerMidPixels header_icon=$headerIconPixels"
    Set-Content -LiteralPath ($RawPath + ".audiobook-title-metrics.txt") -Value $summary
    Write-Host "Audiobook title-list metrics: $summary"
    return ($subheaderPixels -ge 120 -and $headerMidPixels -le 120 -and $headerIconPixels -ge 300)
}

function Assert-OneMatchingLine([string]$Text, [string]$Pattern, [string]$Label) {
    $matches = @($Text -split "`n" | Where-Object { $_ -match $Pattern })
    if ($matches.Count -ne 1) {
        throw "$Label expected exactly one match for $Pattern, found $($matches.Count)"
    }
    Write-Host "OK   $Label has one process root"
}

if ($TitleRow -lt 1 -or $TitleRow -gt 5) {
    throw "TitleRow must be between 1 and 5"
}

$adbPath = Resolve-AdbPath $Adb
$controlScriptPath = Resolve-PathStrict $ControlScript
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$smokeDir = Join-Path (Resolve-Path -LiteralPath (Get-Location).Path).Path (Join-Path $OutDir $stamp)
New-Item -ItemType Directory -Force -Path $smokeDir | Out-Null

& $adbPath devices | Tee-Object -FilePath (Join-Path $smokeDir "adb_devices.txt")
if ($LASTEXITCODE -ne 0) {
    throw "adb devices failed"
}

$versionText = Invoke-AdbText "cat /etc/r1_audiobook_version 2>/dev/null || true"
Set-Content -LiteralPath (Join-Path $smokeDir "r1_audiobook_version.txt") -Value $versionText

$logBeforeBytes = Get-RemoteByteCount "/usr/data/audiobooks/resume-daemon.log"
Set-Content -LiteralPath (Join-Path $smokeDir "resume-daemon-before.bytes.txt") -Value $logBeforeBytes
$logBefore = Invoke-AdbText "tail -n 120 /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true"
Set-Content -LiteralPath (Join-Path $smokeDir "resume-daemon-before.log") -Value $logBefore

$currentState = Capture-SmokeScreen "00-before"

for ($i = 1; $i -le $ResetBacks; $i++) {
    Write-Host "Sending edge-back reset $i/$ResetBacks..."
    $backOutput = Invoke-Control -ControlArgs @("back", "--adb", $adbPath)
    Set-Content -LiteralPath (Join-Path $smokeDir ("00-reset-back-{0}.txt" -f $i)) -Value $backOutput
    Start-Sleep -Seconds $BackSettleSeconds
    $currentState = Capture-SmokeScreen ("00-reset-back-{0}" -f $i)
}

if (!$SkipOpenAudiobooks) {
    if ($currentState -ne "launcher") {
        throw "Opening Audiobooks requires launcher state; current state is [$currentState]. Increase -ResetBacks or start from the main menu."
    }
    $maxOpenAttempts = 1 + [Math]::Max(0, $AudiobooksOpenRetries)
    for ($attempt = 1; $attempt -le $maxOpenAttempts; $attempt++) {
        if ($attempt -eq 1) {
            Write-Host "Opening Audiobooks..."
            $openName = "01-open-audiobooks"
            $screenName = "01-audiobooks"
        } else {
            Write-Host "Retrying Audiobooks open $attempt/$maxOpenAttempts after state [$currentState]..."
            $openName = "01-open-audiobooks-retry-$attempt"
            $screenName = "01-audiobooks-retry-$attempt"
        }
        $openOutput = Invoke-Control -ControlArgs @("preset", "--adb", $adbPath, "--frames", "$TapFrames", "main-audiobooks")
        Set-Content -LiteralPath (Join-Path $smokeDir "$openName.txt") -Value $openOutput
        Start-Sleep -Seconds $AudiobooksSettleSeconds
        $currentState = Capture-SmokeScreen $screenName
        if ($currentState -eq "list") {
            break
        }
        if ($currentState -ne "launcher") {
            break
        }
    }
    if ($currentState -ne "list") {
        throw "Opening Audiobooks expected screen state [list], got [$currentState]"
    }
    if (!$SkipAudiobookTitleListCheck) {
        $rawPath = Get-SmokeRawPath $screenName
        if (!(Test-AudiobookTitleListScreen $rawPath)) {
            throw "Opening Audiobooks reached a list, but it does not match the audiobook title-list signature. It may have opened global Music Genres instead."
        }
        Write-Host "OK   Audiobooks opened the audiobook title-list screen"
    }
}

if (!$SkipStartTitle) {
    if ($currentState -ne "list") {
        throw "Starting a title requires list state; current state is [$currentState]"
    }
    Write-Host "Starting title row $TitleRow..."
    $rowOutput = Invoke-Control -ControlArgs @("row", "--adb", $adbPath, "--frames", "$TapFrames", "$TitleRow")
    Set-Content -LiteralPath (Join-Path $smokeDir "02-title-row.txt") -Value $rowOutput
    Start-Sleep -Seconds $StartSettleSeconds
    $titleTapState = Capture-SmokeScreen "02-after-title-tap"
    if ($titleTapState -ne "now-playing") {
        Write-Host "Retrying title row $TitleRow after state [$titleTapState]..."
        $rowRetryOutput = Invoke-Control -ControlArgs @("row", "--adb", $adbPath, "--frames", "$TapFrames", "$TitleRow")
        Set-Content -LiteralPath (Join-Path $smokeDir "02-title-row-retry.txt") -Value $rowRetryOutput
        Start-Sleep -Seconds $StartSettleSeconds
        Capture-SmokeScreen "02-after-title-retry" "now-playing" | Out-Null
    }

    Write-Host "Waiting $PlaybackSeconds seconds for resume save threshold..."
    Start-Sleep -Seconds $PlaybackSeconds
    Capture-SmokeScreen "03-after-playback" "now-playing" | Out-Null
}

$logAfter = Invoke-AdbText "tail -n 180 /usr/data/audiobooks/resume-daemon.log 2>/dev/null || true"
Set-Content -LiteralPath (Join-Path $smokeDir "resume-daemon-after.log") -Value $logAfter
$logAfterBytes = Get-RemoteByteCount "/usr/data/audiobooks/resume-daemon.log"
$logNew = ""
if ($logAfterBytes -ge $logBeforeBytes) {
    $logNew = Invoke-AdbText "dd if=/usr/data/audiobooks/resume-daemon.log bs=1 skip=$logBeforeBytes 2>/dev/null || true"
}
else {
    $logNew = $logAfter
}
Set-Content -LiteralPath (Join-Path $smokeDir "resume-daemon-new.log") -Value $logNew
$playbackAssertionPassed = $false
if (!$SkipStartTitle) {
    if ($logNew -match "leave audiobook current=non-audiobook|audiobook path=a:\\Music\\") {
        throw "resume daemon log shows non-audiobook playback during smoke test"
    }
    if ($logNew -notmatch "audiobook path=a:\\Audiobooks\\|restore path=a:\\Audiobooks\\") {
        throw "resume daemon log did not show audiobook playback or a fresh audiobook restore response"
    }
    if ($logNew -notmatch "saves=[1-9]|restore path=|after_position_response=") {
        throw "resume daemon log did not show a restore response or saved progress"
    }
    $playbackAssertionPassed = $true
    Write-Host "OK   resume daemon log shows audiobook playback/resume activity"
}
else {
    Write-Host "OK   skipped playback/resume log assertion"
}

$processRoots = Invoke-Control -ControlArgs @("processes", "--adb", $adbPath, "--top-level-only", "--pattern", "r1_audiobook_(resume_daemon|db_watch)|hiby_player")
Set-Content -LiteralPath (Join-Path $smokeDir "process-roots.txt") -Value $processRoots
Assert-OneMatchingLine $processRoots "r1_audiobook_resume_daemon" "resume daemon"
Assert-OneMatchingLine $processRoots "r1_audiobook_db_watch" "DB watcher"

if (!$NoPauseAfter -and $playbackAssertionPassed) {
    Write-Host "Pausing playback..."
    $pauseOutput = Invoke-Control -ControlArgs @("key", "--adb", $adbPath, "playpause")
    Set-Content -LiteralPath (Join-Path $smokeDir "04-pause.txt") -Value $pauseOutput
    Start-Sleep -Seconds 1
    Capture-SmokeScreen "04-after-pause" "now-playing" | Out-Null
}

Write-Host ""
Write-Host "Live audiobook smoke test passed."
Write-Host "Artifacts: $smokeDir"
