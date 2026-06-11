param(
    [Parameter(Mandatory=$true)]
    [string]$Before,

    [Parameter(Mandatory=$true)]
    [string]$After,

    [Parameter(Mandatory=$false)]
    [string]$OutFile,

    [Parameter(Mandatory=$false)]
    [string[]]$IgnorePathPattern = @(
        "/usr/data/audiobooks/resume/",
        "/usr/data/audiobooks/log",
        "/usr/data/.*[.](db-journal|db-wal|db-shm)$",
        "/data/.*[.](db-journal|db-wal|db-shm)$"
    ),

    [Parameter(Mandatory=$false)]
    [switch]$IncludeIgnored
)

$ErrorActionPreference = "Stop"

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Read-Manifest([string]$SnapshotPath) {
    $manifestPath = Join-Path $SnapshotPath "settings-manifest.tsv"
    if (!(Test-Path -LiteralPath $manifestPath)) {
        throw "Missing settings manifest: $manifestPath"
    }

    $entries = @{}
    foreach ($line in Get-Content -LiteralPath $manifestPath) {
        if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith("##")) {
            continue
        }
        if ($line.Contains("|")) {
            $parts = $line -split "\|", 4
            if ($parts.Count -ne 4) {
                continue
            }
            $path = $parts[3].Trim()
            $listing = $parts[2].Trim()
        }
        elseif ($line.Contains("`t")) {
            $parts = $line -split "`t", 4
            if ($parts.Count -ne 4) {
                continue
            }
            $path = $parts[3].Trim()
            $listing = $parts[2].Trim()
        }
        else {
            $parts = $line -split "\s+", 3
            if ($parts.Count -ne 3) {
                continue
            }
            $path = $parts[2].Trim()
            $listing = ""
        }
        if ([string]::IsNullOrWhiteSpace($path)) {
            continue
        }
        $entries[$path] = [pscustomobject]@{
            Size = $parts[0].Trim()
            Hash = $parts[1].Trim()
            Listing = $listing
            Path = $path
        }
    }
    return $entries
}

function Test-IgnoredPath([string]$PathValue) {
    foreach ($pattern in $IgnorePathPattern) {
        if ($PathValue -match $pattern) {
            return $true
        }
    }
    return $false
}

function Read-PulledFileManifest([string]$SnapshotPath) {
    $filesDir = Join-Path $SnapshotPath "files"
    $entries = @{}
    if (!(Test-Path -LiteralPath $filesDir)) {
        return $entries
    }

    foreach ($file in Get-ChildItem -LiteralPath $filesDir -File) {
        $hash = Get-FileHash -LiteralPath $file.FullName -Algorithm MD5
        $entries[$file.Name] = [pscustomobject]@{
            Size = $file.Length
            Hash = $hash.Hash.ToLowerInvariant()
            Path = $file.FullName
            Name = $file.Name
        }
    }
    return $entries
}

function Add-Line([System.Collections.Generic.List[string]]$Lines, [string]$Line = "") {
    $Lines.Add($Line) | Out-Null
}

function Add-ChangeSection(
    [System.Collections.Generic.List[string]]$Lines,
    [string]$Title,
    [array]$Changes
) {
    Add-Line $Lines ""
    Add-Line $Lines $Title
    Add-Line $Lines ("-" * $Title.Length)
    if ($Changes.Count -eq 0) {
        Add-Line $Lines "No changes."
        return
    }
    foreach ($change in $Changes) {
        Add-Line $Lines $change
    }
}

$beforePath = Resolve-PathStrict $Before
$afterPath = Resolve-PathStrict $After
if (-not $OutFile) {
    $OutFile = Join-Path $afterPath "settings-snapshot-comparison.txt"
}

$beforeManifest = Read-Manifest $beforePath
$afterManifest = Read-Manifest $afterPath
$manifestChanges = New-Object System.Collections.Generic.List[string]
$ignoredChanges = New-Object System.Collections.Generic.List[string]

$allPaths = @($beforeManifest.Keys) + @($afterManifest.Keys) | Sort-Object -Unique
foreach ($path in $allPaths) {
    $beforeEntry = $beforeManifest[$path]
    $afterEntry = $afterManifest[$path]
    $changeLine = $null
    if ($null -eq $beforeEntry) {
        $changeLine = "ADDED   $($afterEntry.Size) $($afterEntry.Hash) $path"
    }
    elseif ($null -eq $afterEntry) {
        $changeLine = "REMOVED $($beforeEntry.Size) $($beforeEntry.Hash) $path"
    }
    elseif (($beforeEntry.Size -ne $afterEntry.Size) -or ($beforeEntry.Hash -ne $afterEntry.Hash)) {
        $changeLine = "CHANGED $($beforeEntry.Size)->$($afterEntry.Size) $($beforeEntry.Hash)->$($afterEntry.Hash) $path"
    }

    if ($changeLine) {
        if ((Test-IgnoredPath $path) -and -not $IncludeIgnored) {
            $ignoredChanges.Add($changeLine) | Out-Null
        }
        else {
            $manifestChanges.Add($changeLine) | Out-Null
        }
    }
}

$beforePulled = Read-PulledFileManifest $beforePath
$afterPulled = Read-PulledFileManifest $afterPath
$pulledChanges = New-Object System.Collections.Generic.List[string]
$changedPulledNames = New-Object System.Collections.Generic.List[string]
$allPulledNames = @($beforePulled.Keys) + @($afterPulled.Keys) | Sort-Object -Unique
foreach ($name in $allPulledNames) {
    $beforeEntry = $beforePulled[$name]
    $afterEntry = $afterPulled[$name]
    if ($null -eq $beforeEntry) {
        $pulledChanges.Add("ADDED   $($afterEntry.Size) $($afterEntry.Hash) $name") | Out-Null
    }
    elseif ($null -eq $afterEntry) {
        $pulledChanges.Add("REMOVED $($beforeEntry.Size) $($beforeEntry.Hash) $name") | Out-Null
    }
    elseif (($beforeEntry.Size -ne $afterEntry.Size) -or ($beforeEntry.Hash -ne $afterEntry.Hash)) {
        $pulledChanges.Add("CHANGED $($beforeEntry.Size)->$($afterEntry.Size) $($beforeEntry.Hash)->$($afterEntry.Hash) $name") | Out-Null
        $changedPulledNames.Add($name) | Out-Null
    }
}

$lines = New-Object System.Collections.Generic.List[string]
Add-Line $lines "R1 settings snapshot comparison"
Add-Line $lines "Before: $beforePath"
Add-Line $lines "After : $afterPath"
Add-ChangeSection $lines "Writable State Manifest Changes" $manifestChanges.ToArray()
Add-ChangeSection $lines "Pulled Candidate File Changes" $pulledChanges.ToArray()
if ($changedPulledNames.Count -gt 0) {
    Add-Line $lines ""
    Add-Line $lines "Binary Detail For Changed Pulled Files"
    Add-Line $lines "--------------------------------------"
    $binaryCompare = Join-Path $PSScriptRoot "compare_binary_settings.py"
    if (Test-Path -LiteralPath $binaryCompare) {
        foreach ($name in $changedPulledNames | Select-Object -First 5) {
            Add-Line $lines ""
            Add-Line $lines "## $name"
            $beforeFile = $beforePulled[$name].Path
            $afterFile = $afterPulled[$name].Path
            $detail = & python $binaryCompare $beforeFile $afterFile --context 48 --limit 4 2>&1
            if ($LASTEXITCODE -ne 0) {
                Add-Line $lines "WARN binary comparison failed for $name"
            }
            foreach ($line in $detail) {
                Add-Line $lines $line
            }
        }
    }
    else {
        Add-Line $lines "WARN missing $binaryCompare"
    }
}
if ($ignoredChanges.Count -gt 0) {
    Add-ChangeSection $lines "Ignored Volatile Manifest Changes" $ignoredChanges.ToArray()
}

$outParent = Split-Path -Parent $OutFile
if ($outParent -and !(Test-Path -LiteralPath $outParent)) {
    New-Item -ItemType Directory -Force -Path $outParent | Out-Null
}
$lines | Set-Content -LiteralPath $OutFile -Encoding UTF8

Write-Host "Comparison saved to $OutFile"
if ($manifestChanges.Count -gt 0 -or $pulledChanges.Count -gt 0) {
    Write-Host "Review changed paths above first; one of them may be the persisted UI setting."
}
else {
    Write-Host "No non-ignored changes found. Re-run with -IncludeIgnored if the UI setting may share a volatile file."
}
