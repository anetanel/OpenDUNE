<#
.SYNOPSIS
  Installs OpenDUNE (with Hebrew support) on Windows.

.DESCRIPTION
  Prompts for the install location (default: C:\Program Files\OpenDUNE; pass
  -InstallDir to skip the prompt). Downloads the latest OpenDUNE Windows release and the Hebrew data files,
  and installs the Dune II 1.07 (EU/US) game data. By default the Dune II game data is downloaded automatically (from
  MyAbandonware). Alternatively, supply your own copy
  with -GamePath (a folder or a .zip containing the *.PAK files).

.EXAMPLE
  irm https://anetanel.github.io/OpenDUNE/tools/install-opendune.ps1 | iex
  .\install-opendune.ps1 -GamePath C:\Games\Dune2
#>
param(
  [string]$GamePath,
  [string]$InstallDir,
  [ValidateSet('win64','win32')][string]$Arch = 'win64',
  [string]$Repo = 'anetanel/OpenDUNE',
  [string]$Branch = 'master'
)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = 'SilentlyContinue'   # cmdlet progress (Invoke-WebRequest/Expand-Archive) is very slow; Get-File shows its own
Add-Type -AssemblyName System.Net.Http

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Info($msg) { Write-Host "    $msg" }

# Streaming download with a progress bar and file name
function Get-File([string]$Url, [string]$OutFile, [string]$Label, $Cookies) {
  Info "Downloading $Label"
  Info "  from $Url"
  $handler = New-Object System.Net.Http.HttpClientHandler
  if ($Cookies) { $handler.CookieContainer = $Cookies }
  $client = New-Object System.Net.Http.HttpClient($handler)
  $client.DefaultRequestHeaders.UserAgent.ParseAdd('Mozilla/5.0 opendune-installer')
  $in = $null; $out = $null
  $ProgressPreference = 'Continue'
  try {
    $resp = $client.GetAsync($Url, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
    $resp.EnsureSuccessStatusCode() | Out-Null
    $total = $resp.Content.Headers.ContentLength
    $in  = $resp.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
    $out = [IO.File]::Create($OutFile)
    $buf = New-Object byte[] 81920
    $done = 0; $lastPct = -1
    while (($n = $in.Read($buf, 0, $buf.Length)) -gt 0) {
      $out.Write($buf, 0, $n)
      $done += $n
      if ($total) {
        $pct = [int](100 * $done / $total)
        if ($pct -ne $lastPct) {
          $lastPct = $pct
          Write-Progress -Activity "Downloading $Label" -Status ("{0:N1} / {1:N1} MB ({2}%)" -f ($done/1MB), ($total/1MB), $pct) -PercentComplete $pct
        }
      } else {
        Write-Progress -Activity "Downloading $Label" -Status ("{0:N1} MB" -f ($done/1MB))
      }
    }
    Write-Progress -Activity "Downloading $Label" -Completed
    Info ("  done ({0:N1} MB)" -f ($done/1MB))
  }
  finally {
    if ($in) { $in.Dispose() }
    if ($out) { $out.Dispose() }
    $client.Dispose()
    $ProgressPreference = 'SilentlyContinue'
  }
}

# Extract a zip, reporting each top-level step
function Expand-Zip([string]$Zip, [string]$Dest, [string]$Label) {
  Info "Extracting $Label ($(Split-Path $Zip -Leaf))"
  Expand-Archive -LiteralPath $Zip -DestinationPath $Dest -Force
}

# Ask for the install location (default: Program Files)
if (-not $InstallDir) {
  $default = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'OpenDUNE'
  $answer = Read-Host "Installation location [$default]"
  $InstallDir = if ([string]::IsNullOrWhiteSpace($answer)) { $default } else { $answer.Trim().Trim('"') }
}
$InstallDir = [IO.Path]::GetFullPath($InstallDir)
Write-Host "OpenDUNE will be installed to: $InstallDir" -ForegroundColor Green

# Program Files etc. need admin rights
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$pf = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ }
if (-not $isAdmin -and ($pf | Where-Object { $InstallDir.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) })) {
  throw "Installing to '$InstallDir' requires administrator rights. Re-run PowerShell as Administrator, or choose another location."
}

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("opendune-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $tmp | Out-Null
try {
  # 1. Game data (user supplied via -GamePath, otherwise downloaded)
  Step '[1/4] Dune II game data'
  if (-not $GamePath) {
    # MyAbandonware hands out a tokenized link, so load the game page first to get a session
    $gamePageUrl = 'https://www.myabandonware.com/game/dune-ii-the-building-of-a-dynasty-1e7'
    $downloadId  = 'lgxx-dune-ii-the-building-of-a-dynasty'   # EN-FR-DE / European version (4 MB)
    # Other known IDs on the same page:
    #   lgxw-dune-ii-the-building-of-a-dynasty   -> 4 MB
    #   lt0i-dune-ii-the-building-of-a-dynasty   -> ISO 11 MB
    #   p9ei-dune-ii-the-building-of-a-dynasty   -> Disc Image 4 MB
    Info 'Opening MyAbandonware game page (session cookie)'
    $session = New-Object Microsoft.PowerShell.Commands.WebRequestSession
    $null = Invoke-WebRequest -Uri $gamePageUrl -WebSession $session -UseBasicParsing
    $GamePath = Join-Path $tmp 'game.zip'
    Get-File "https://www.myabandonware.com/download/$downloadId" $GamePath "Dune II game data ($downloadId)" $session.Cookies
  }
  $GamePath = $GamePath.Trim('"')
  if (-not (Test-Path $GamePath)) { throw "Not found: $GamePath" }
  if ((Get-Item $GamePath).PSIsContainer) { $gameSrc = $GamePath }
  else {
    $gameSrc = Join-Path $tmp 'game'
    Expand-Zip $GamePath $gameSrc 'game data'
  }
  $dunePak = Get-ChildItem -Path $gameSrc -Recurse -Filter 'DUNE.PAK' | Select-Object -First 1
  if (-not $dunePak) { throw "DUNE.PAK not found under $gameSrc - is this a Dune II 1.07 install?" }

  # 2. OpenDUNE release
  Step '[2/4] OpenDUNE release'
  Info "Querying latest release of $Repo"
  $rel = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest" -Headers @{ 'User-Agent' = 'opendune-installer' }
  $asset = $rel.assets | Where-Object { $_.name -match $Arch -and $_.name -match '\.zip$' } | Select-Object -First 1
  if (-not $asset) { throw "No $Arch .zip asset in release $($rel.tag_name) of $Repo" }
  $zip = Join-Path $tmp $asset.name
  Info "Found $($asset.name) (release $($rel.tag_name))"
  Get-File $asset.browser_download_url $zip $asset.name
  $rdir = Join-Path $tmp 'release'
  Expand-Zip $zip $rdir 'OpenDUNE release'

  # 3. Hebrew data files
  Step '[3/4] Hebrew data files'
  $hebZip = Join-Path $tmp 'hebrew.zip'
  $hebAsset = $rel.assets | Where-Object { $_.name -match 'hebrew.*\.zip$' } | Select-Object -First 1
  if ($hebAsset) {
    Info "Using $($hebAsset.name) from release $($rel.tag_name)"
    Get-File $hebAsset.browser_download_url $hebZip $hebAsset.name
  } else {
    Info "Release $($rel.tag_name) has no Hebrew asset; falling back to branch '$Branch' (may not match the release)"
    Get-File "https://raw.githubusercontent.com/$Repo/$Branch/hebrew/dist/dune2-hebrew.zip" $hebZip 'dune2-hebrew.zip'
  }

  # 4. Install
  Step "[4/4] Installing to $InstallDir"
  New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
  $exe = Get-ChildItem -Path $rdir -Recurse -Filter 'opendune.exe' | Select-Object -First 1
  if (-not $exe) { throw 'opendune.exe not found in release archive' }
  Info 'Copying OpenDUNE program files'
  Copy-Item -Path (Join-Path $exe.DirectoryName '*') -Destination $InstallDir -Recurse -Force
  $data = Join-Path $InstallDir 'data'
  New-Item -ItemType Directory -Path $data -Force | Out-Null
  Info 'Copying Dune II game data'
  Copy-Item -Path (Join-Path $dunePak.DirectoryName '*') -Destination $data -Recurse -Force
  Expand-Zip $hebZip $data 'Hebrew files'

  # Start-menu shortcut
  Info 'Creating Start-menu shortcut'
  $lnk = Join-Path ([Environment]::GetFolderPath('Programs')) 'OpenDUNE.lnk'
  $sh = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
  $sh.TargetPath = Join-Path $InstallDir 'opendune.exe'
  $sh.WorkingDirectory = $InstallDir
  $sh.Save()

  Write-Host "`nDone. Launch OpenDUNE from the Start menu or $InstallDir\opendune.exe"
}
finally { Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue }
