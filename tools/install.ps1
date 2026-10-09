param(
    [string]$Repo = $env:ANYPS5_REPO,
    [string]$Api = $env:ANYPS5_API_URL,
    [string]$Tag = "",
    [string]$Prefix = "",
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrEmpty($Repo)) { $Repo = "pedro3z0/AnyPS5" }
if ([string]::IsNullOrEmpty($Api)) { $Api = "https://api.github.com" }
if ([string]::IsNullOrEmpty($Prefix)) { $Prefix = Join-Path $env:LOCALAPPDATA "AnyPS5" }
$Bin = Join-Path $Prefix "bin"
$Launcher = Join-Path $Bin "launcher.exe"

if ($Uninstall) {
    Write-Host "removing $Prefix"
    Remove-Item -Recurse -Force $Prefix -ErrorAction SilentlyContinue
    return
}

if ([string]::IsNullOrEmpty($Tag)) {
    $latest = Invoke-RestMethod -Uri "$Api/repos/$Repo/releases/latest"
    $Tag = $latest.tag_name
}
if ([string]::IsNullOrEmpty($Tag)) { throw "no releases found for $Repo" }
$release = Invoke-RestMethod -Uri "$Api/repos/$Repo/releases/tags/$Tag"
$asset = $release.assets | Where-Object { $_.browser_download_url -match "win64\.zip$" } | Select-Object -First 1
if ($null -eq $asset) { throw "no Windows package asset in release $Tag" }
$work = Join-Path ([System.IO.Path]::GetTempPath()) "anyps5-install"
New-Item -ItemType Directory -Force -Path $work | Out-Null
try {
    Write-Host "downloading $($asset.browser_download_url)"
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile (Join-Path $work "anyps5.zip")
    if (Test-Path $Prefix) { Remove-Item -Recurse -Force $Prefix }
    Expand-Archive -Path (Join-Path $work "anyps5.zip") -DestinationPath $Prefix
    $dirs = Get-ChildItem -Directory $Prefix
    if ($dirs.Count -eq 1 -and (Test-Path (Join-Path $dirs[0].FullName "bin"))) {
        Get-ChildItem $dirs[0].FullName | Move-Item -Destination $Prefix -Force
        Remove-Item -Recurse -Force $dirs[0].FullName
    }
} finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath("StartMenu")) "AnyPS5.lnk"))
$shortcut.TargetPath = $Launcher
$shortcut.WorkingDirectory = $Bin
$shortcut.Save()
Write-Host "installed $Tag to $Prefix"
