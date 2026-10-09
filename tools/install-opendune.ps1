<#
.SYNOPSIS
  Installs OpenDUNE (with Hebrew support) on Windows.

.DESCRIPTION
  Downloads the latest OpenDUNE Windows release and the Hebrew data files,
  and installs the Dune II 1.07 (EU/US) game data. By default the Dune II game data is downloaded automatically (from
  MyAbandonware). Alternatively, supply your own copy
  with -GamePath (a folder or a .zip containing the *.PAK files).

.EXAMPLE
  irm https://raw.githubusercontent.com/anetanel/OpenDUNE/master/tools/install-opendune.ps1 | iex
  .\install-opendune.ps1 -GamePath C:\Games\Dune2
#>
param(
  [string]$GamePath,
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
  # 1. Game data (user supplied via -GamePath, otherwise downloaded)
  if (-not $GamePath) {
    # MyAbandonware hands out a tokenized link, so load the game page first to get a session
    $gamePageUrl = 'https://www.myabandonware.com/game/dune-ii-the-building-of-a-dynasty-1e7'
    $downloadId  = 'lgxx-dune-ii-the-building-of-a-dynasty'   # EN-FR-DE / European version (4 MB)
    # Other known IDs on the same page:
    #   lgxw-dune-ii-the-building-of-a-dynasty   -> 4 MB
    #   lt0i-dune-ii-the-building-of-a-dynasty   -> ISO 11 MB
    #   p9ei-dune-ii-the-building-of-a-dynasty   -> Disc Image 4 MB
    Write-Host 'Downloading Dune II game data...'
    $session = New-Object Microsoft.PowerShell.Commands.WebRequestSession
    $null = Invoke-WebRequest -Uri $gamePageUrl -WebSession $session -UseBasicParsing
    $GamePath = Join-Path $tmp 'game.zip'
    $null = Invoke-WebRequest -Uri "https://www.myabandonware.com/download/$downloadId" `
                              -WebSession $session -UseBasicParsing -MaximumRedirection 5 -OutFile $GamePath
  }
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
