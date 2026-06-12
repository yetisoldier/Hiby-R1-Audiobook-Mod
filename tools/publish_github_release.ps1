[CmdletBinding(PositionalBinding=$false)]
param(
    [Parameter(Mandatory=$false)]
    [string]$Repository = "yetisoldier/Hiby-R1-Audiobook-Mod",

    [Parameter(Mandatory=$true)]
    [string]$Tag,

    [Parameter(Mandatory=$false)]
    [string]$Name = "",

    [Parameter(Mandatory=$false)]
    [string]$TargetCommitish = "",

    [Parameter(Mandatory=$false)]
    [string]$BodyFile = "",

    [Parameter(Mandatory=$false)]
    [string]$Body = "",

    [Parameter(Mandatory=$false)]
    [string]$GitHubUser = "yetisoldier",

    [switch]$Draft,

    [switch]$Prerelease,

    [switch]$ReplaceAssets,

    [switch]$VerifyOnly,

    [Parameter(Mandatory=$false)]
    [string[]]$Assets = @()
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Resolve-PathStrict([string]$PathValue) {
    if (!(Test-Path -LiteralPath $PathValue)) {
        throw "Missing path: $PathValue"
    }
    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Get-OptionalEnvToken {
    foreach ($name in @("GITHUB_TOKEN", "GH_TOKEN", "GITHUB_PAT")) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if (![string]::IsNullOrWhiteSpace($value)) {
            return $value
        }
    }
    return ""
}

function Get-GitHubTokenFromCredentialManager([string]$UserName) {
    $tmp = Join-Path $env:TEMP ("gcm-github-release-{0}.txt" -f ([Guid]::NewGuid().ToString("N")))
    $lines = @("protocol=https", "host=github.com")
    if (![string]::IsNullOrWhiteSpace($UserName)) {
        $lines += "username=$UserName"
    }

    try {
        [System.IO.File]::WriteAllText($tmp, (($lines -join "`n") + "`n`n"), [System.Text.Encoding]::ASCII)
        $credOutput = cmd /c ('git credential-manager get < "' + $tmp + '"') 2>$null
        foreach ($line in $credOutput) {
            if ($line -like "password=*") {
                return $line.Substring(9)
            }
        }
    } finally {
        if (Test-Path -LiteralPath $tmp) {
            Remove-Item -LiteralPath $tmp -Force
        }
    }

    return ""
}

function Get-GitHubToken([switch]$Required) {
    $token = Get-OptionalEnvToken
    if (![string]::IsNullOrWhiteSpace($token)) {
        return $token
    }

    $token = Get-GitHubTokenFromCredentialManager -UserName $GitHubUser
    if (![string]::IsNullOrWhiteSpace($token)) {
        return $token
    }

    if ($Required) {
        throw "No GitHub token found. Log in with Git Credential Manager first, for example: git credential-manager github login"
    }

    return ""
}

function New-GitHubHeaders([string]$Token) {
    $headers = @{
        Accept = "application/vnd.github+json"
        "X-GitHub-Api-Version" = "2022-11-28"
        "User-Agent" = "hiby-r1-audiobook-release-tool"
    }
    if (![string]::IsNullOrWhiteSpace($Token)) {
        $headers.Authorization = "Bearer $Token"
    }
    return $headers
}

function Read-ErrorBody($ErrorRecord) {
    $response = $ErrorRecord.Exception.Response
    if ($null -eq $response) {
        return ""
    }

    try {
        $stream = $response.GetResponseStream()
        if ($null -eq $stream) {
            return ""
        }
        $reader = New-Object System.IO.StreamReader($stream)
        return $reader.ReadToEnd()
    } catch {
        return ""
    }
}

function Invoke-GitHubJson([string]$Method, [string]$Uri, $Headers, $Payload = $null, [switch]$AllowNotFound) {
    try {
        if ($null -eq $Payload) {
            return Invoke-RestMethod -Method $Method -Uri $Uri -Headers $Headers
        }

        $json = $Payload | ConvertTo-Json -Depth 8
        return Invoke-RestMethod -Method $Method -Uri $Uri -Headers $Headers -Body $json -ContentType "application/json"
    } catch {
        $response = $_.Exception.Response
        if ($AllowNotFound -and $null -ne $response -and [int]$response.StatusCode -eq 404) {
            return $null
        }

        $bodyText = Read-ErrorBody $_
        if (![string]::IsNullOrWhiteSpace($bodyText)) {
            throw "GitHub API $Method $Uri failed: $($_.Exception.Message)`n$bodyText"
        }
        throw "GitHub API $Method $Uri failed: $($_.Exception.Message)"
    }
}

function Get-ContentTypeForAsset([string]$PathValue) {
    $extension = [System.IO.Path]::GetExtension($PathValue).ToLowerInvariant()
    switch ($extension) {
        ".txt" { return "text/plain" }
        ".md" { return "text/markdown" }
        ".json" { return "application/json" }
        default { return "application/octet-stream" }
    }
}

function Get-ReleaseByTag([string]$Repo, [string]$ReleaseTag, $Headers) {
    $encodedTag = [System.Uri]::EscapeDataString($ReleaseTag)
    $uri = "https://api.github.com/repos/$Repo/releases/tags/$encodedTag"
    return Invoke-GitHubJson -Method "Get" -Uri $uri -Headers $Headers -AllowNotFound
}

function Write-ReleaseSummary($Release, [string[]]$ExpectedAssets) {
    if ($null -eq $Release) {
        throw "GitHub release object for tag '$Tag' was not found. A web tag page is not enough; the API release object must exist before downloads work."
    }

    Write-Host "Release: $($Release.html_url)"
    Write-Host "Draft: $($Release.draft)"
    Write-Host "Prerelease: $($Release.prerelease)"
    Write-Host "Assets: $($Release.assets.Count)"

    $assetNames = @()
    foreach ($asset in $Release.assets) {
        $assetNames += $asset.name
        Write-Host ("- {0} ({1} bytes)" -f $asset.name, $asset.size)
    }

    foreach ($assetPath in $ExpectedAssets) {
        $assetName = [System.IO.Path]::GetFileName($assetPath)
        if ($assetNames -notcontains $assetName) {
            throw "Release is missing expected asset: $assetName"
        }
    }
}

$assetSpecs = @()
foreach ($asset in $Assets) {
    foreach ($part in ($asset -split ",")) {
        $trimmed = $part.Trim()
        if (![string]::IsNullOrWhiteSpace($trimmed)) {
            $assetSpecs += $trimmed
        }
    }
}

$assetPaths = @()
foreach ($asset in $assetSpecs) {
    $assetPaths += Resolve-PathStrict $asset
}

if ($VerifyOnly) {
    $headers = New-GitHubHeaders -Token (Get-GitHubToken)
    $release = Get-ReleaseByTag -Repo $Repository -ReleaseTag $Tag -Headers $headers
    Write-ReleaseSummary -Release $release -ExpectedAssets $assetPaths
    return
}

$token = Get-GitHubToken -Required
$headers = New-GitHubHeaders -Token $token

if ([string]::IsNullOrWhiteSpace($Name)) {
    $Name = $Tag
}

if ([string]::IsNullOrWhiteSpace($TargetCommitish)) {
    $TargetCommitish = (git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($TargetCommitish)) {
        throw "Unable to resolve current HEAD for target_commitish"
    }
}

if (![string]::IsNullOrWhiteSpace($BodyFile)) {
    $resolvedBodyFile = Resolve-PathStrict $BodyFile
    $Body = Get-Content -LiteralPath $resolvedBodyFile -Raw
}

$release = Get-ReleaseByTag -Repo $Repository -ReleaseTag $Tag -Headers $headers
if ($null -eq $release) {
    $createPayload = [ordered]@{
        tag_name = $Tag
        target_commitish = $TargetCommitish
        name = $Name
        body = $Body
        draft = [bool]$Draft
        prerelease = [bool]$Prerelease
    }
    $release = Invoke-GitHubJson -Method "Post" -Uri "https://api.github.com/repos/$Repository/releases" -Headers $headers -Payload $createPayload
    Write-Host "Created release: $($release.html_url)"
} else {
    Write-Host "Using existing release: $($release.html_url)"
}

$uploadBase = $release.upload_url -replace "\{.*$", ""

foreach ($path in $assetPaths) {
    $assetName = [System.IO.Path]::GetFileName($path)
    $existingAsset = $null
    foreach ($asset in $release.assets) {
        if ($asset.name -eq $assetName) {
            $existingAsset = $asset
            break
        }
    }

    if ($null -ne $existingAsset) {
        if (!$ReplaceAssets) {
            throw "Release already has asset '$assetName'. Re-run with -ReplaceAssets if you intentionally want to replace it."
        }

        Invoke-GitHubJson -Method "Delete" -Uri $existingAsset.url -Headers $headers | Out-Null
        Write-Host "Deleted existing asset: $assetName"
    }

    $uploadUri = $uploadBase + "?name=" + [System.Uri]::EscapeDataString($assetName)
    $contentType = Get-ContentTypeForAsset $path
    try {
        $uploaded = Invoke-RestMethod -Method Post -Uri $uploadUri -Headers $headers -InFile $path -ContentType $contentType
        Write-Host ("Uploaded: {0} ({1} bytes)" -f $uploaded.name, $uploaded.size)
    } catch {
        $bodyText = Read-ErrorBody $_
        if (![string]::IsNullOrWhiteSpace($bodyText)) {
            throw "GitHub asset upload failed for '$assetName': $($_.Exception.Message)`n$bodyText"
        }
        throw "GitHub asset upload failed for '$assetName': $($_.Exception.Message)"
    }
}

$release = Get-ReleaseByTag -Repo $Repository -ReleaseTag $Tag -Headers $headers
Write-ReleaseSummary -Release $release -ExpectedAssets $assetPaths
