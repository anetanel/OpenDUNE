<#
.SYNOPSIS
  Installs OpenDUNE (with Hebrew support) on Windows.

.DESCRIPTION
  Downloads the latest OpenDUNE Windows release and the Hebrew data files,
  and installs your own copy of the Dune II 1.07 (EU/US) game data.
  The game data is not bundled: Dune II is still under copyright, so you
  must supply it yourself via -GamePath (a folder or a .zip containing the
  *.PAK files). If omitted, the script asks for it. For testing/development
  you can pass -DownloadGame instead, which runs tools/download_game.ps1 to
  fetch the game zip from MyAbandonware.

.EXAMPLE
  .\install-opendune.ps1 -GamePath C:\Games\Dune2
  .\install-opendune.ps1 -DownloadGame
  irm https://raw.githubusercontent.com/anetanel/OpenDUNE/master/tools/install-opendune.ps1 | iex
#>
param(
  [string]$GamePath,
  [switch]$DownloadGame,
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'OpenDUNE'),
  [ValidateSet('win64','win32')][string]$Arch = 'win64',
  [string]$Repo = 'anetanel/OpenDUNE',
  [string]$Branch = 'master'
)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is much faster without it

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("opendune-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
  # 1. Game data (user supplied, or fetched with -DownloadGame)
  if ($DownloadGame -and -not $GamePath) {
    $dl = Join-Path $PSScriptRoot 'download_game.ps1'
    if (-not $PSScriptRoot -or -not (Test-Path $dl)) {   # e.g. run via irm | iex
      $dl = Join-Path $tmp 'download_game.ps1'
      Invoke-WebRequest "https://raw.githubusercontent.com/$Repo/$Branch/tools/download_game.ps1" -OutFile $dl
    }
    $GamePath = Join-Path $tmp 'game.zip'
    & $dl -OutFile $GamePath
  }
  if (-not $GamePath) { $GamePath = Read-Host 'Path to your Dune II game folder or .zip (containing the .PAK files)' }
  $GamePath = $GamePath.Trim('"')
  if (-not (Test-Path $GamePath)) { throw "Not found: $GamePath" }
  if ((Get-Item $GamePath).PSIsContainer) { $gameSrc = $GamePath }
  else {
    $gameSrc = Join-Path $tmp 'game'
    Expand-Archive -LiteralPath $GamePath -DestinationPath $gameSrc
  }
  $dunePak = Get-ChildItem -Path $gameSrc -Recurse -Filter 'DUNE.PAK' | Select-Object -First 1
  if (-not $dunePak) { throw "DUNE.PAK not found under $gameSrc - is this a Dune II 1.07 install?" }

  # 2. OpenDUNE release
  Write-Host 'Fetching latest OpenDUNE release...'
  $rel = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest" -Headers @{ 'User-Agent' = 'opendune-installer' }
  $asset = $rel.assets | Where-Object { $_.name -match $Arch -and $_.name -match '\.zip$' } | Select-Object -First 1
  if (-not $asset) { throw "No $Arch .zip asset in release $($rel.tag_name) of $Repo" }
  $zip = Join-Path $tmp $asset.name
  Invoke-WebRequest $asset.browser_download_url -OutFile $zip
  $rdir = Join-Path $tmp 'release'
  Expand-Archive -LiteralPath $zip -DestinationPath $rdir

  # 3. Hebrew data files
  Write-Host 'Fetching Hebrew files...'
  $hebZip = Join-Path $tmp 'hebrew.zip'
  Invoke-WebRequest "https://raw.githubusercontent.com/$Repo/$Branch/hebrew/dist/dune2-hebrew.zip" -OutFile $hebZip

  # 4. Install
  Write-Host "Installing to $InstallDir"
  New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
  $exe = Get-ChildItem -Path $rdir -Recurse -Filter 'opendune.exe' | Select-Object -First 1
  if (-not $exe) { throw 'opendune.exe not found in release archive' }
  Copy-Item -Path (Join-Path $exe.DirectoryName '*') -Destination $InstallDir -Recurse -Force
  $data = Join-Path $InstallDir 'data'
  New-Item -ItemType Directory -Path $data -Force | Out-Null
  Copy-Item -Path (Join-Path $dunePak.DirectoryName '*') -Destination $data -Recurse -Force
  Expand-Archive -LiteralPath $hebZip -DestinationPath $data -Force

  # Start-menu shortcut
  $lnk = Join-Path ([Environment]::GetFolderPath('Programs')) 'OpenDUNE.lnk'
  $sh = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
  $sh.TargetPath = Join-Path $InstallDir 'opendune.exe'
  $sh.WorkingDirectory = $InstallDir
  $sh.Save()

  Write-Host "Done. Launch OpenDUNE from the Start menu or $InstallDir\opendune.exe"
}
finally { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
