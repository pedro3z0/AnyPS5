param(
    [string]$Repo = $env:ANYPS5_REPO,
    [string]$Api = $env:ANYPS5_API_URL,
    [string]$Tag = "",
    [switch]$Check
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrEmpty($Repo)) { $Repo = "pedro3z0/AnyPS5" }
if ([string]::IsNullOrEmpty($Api)) { $Api = "https://api.github.com" }
$Root = Split-Path -Parent $PSScriptRoot
$Launcher = Join-Path $Root "bin\anyps5-launcher.exe"

function CurrentVersion {
    try {
        return (& $Launcher --version).Trim()
    } catch {
        return "unknown"
    }
}

if ([string]::IsNullOrEmpty($Tag)) {
    $latest = Invoke-RestMethod -Uri "$Api/repos/$Repo/releases/latest"
    $Tag = $latest.tag_name
}
if ([string]::IsNullOrEmpty($Tag)) { Write-Error "FAIL: could not reach releases for $Repo"; exit 2 }
$current = CurrentVersion
if ($current -eq "unknown" -or $current.TrimStart("v") -ne $Tag.TrimStart("v")) {
    Write-Output "ANYPS5_UPDATE_AVAILABLE $current -> $Tag"
    if ($Check) { exit 100 }
} else {
    Write-Output "ANYPS5_UP_TO_DATE $current"
    exit 0
}
function Get-FileSha256($path) {
    return (Get-FileHash -Path $path -Algorithm SHA256).Hash
}
$release = Invoke-RestMethod -Uri "$Api/repos/$Repo/releases/tags/$Tag"
$asset = $release.assets | Where-Object { $_.browser_download_url -match "win64\.zip$" } | Select-Object -First 1
if ($null -eq $asset) { Write-Error "FAIL: no Windows package asset in release $Tag"; exit 2 }
$sums = $release.assets | Where-Object { $_.browser_download_url -match "SHA256SUMS\.txt$" } | Select-Object -First 1
if ($null -eq $sums) { Write-Error "FAIL: no SHA256SUMS.txt in release $Tag"; exit 2 }
$work = Join-Path ([System.IO.Path]::GetTempPath()) "anyps5-update"
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    Write-Host "downloading $($asset.browser_download_url)"
    $archive = Join-Path $work "anyps5.zip"
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $archive
    $sumsFile = Join-Path $work "SHA256SUMS.txt"
    Invoke-WebRequest -Uri $sums.browser_download_url -OutFile $sumsFile
    $name = Split-Path -Leaf $archive
    $expected = (Select-String -Path $sumsFile -Pattern " $name$" | Select-Object -First 1).Line.Split()[0]
    if ([string]::IsNullOrEmpty($expected) -or (Get-FileSha256 $archive) -ne $expected) {
        throw "FAIL: checksum mismatch for $($asset.browser_download_url)"
    }
    $stage = Join-Path $work "stage"
    Expand-Archive -Path $archive -DestinationPath $stage
    $inner = Get-ChildItem -Directory $stage
    $stageRoot = if ($inner.Count -eq 1 -and (Test-Path (Join-Path $inner[0].FullName "bin"))) { $inner[0].FullName } else { $stage }
    if (-not (Test-Path (Join-Path $stageRoot "bin\anyps5-launcher.exe"))) { throw "FAIL: $($asset.browser_download_url) is not a launcher package" }
    & (Join-Path $stageRoot "bin\anyps5-launcher.exe") --version | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "FAIL: packaged launcher does not start" }
    foreach ($dir in @("bin", "tools", "lib", "core", "docs")) {
        $target = Join-Path $Root $dir
        if (Test-Path $target) { Remove-Item -Recurse -Force $target }
        Move-Item -Path (Join-Path $stageRoot $dir) -Destination $target -Force
    }
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
Write-Output "installed $Tag"
