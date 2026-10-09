# Download Dune II (DOS EN-FR-DE) from MyAbandonware, handling the dynamic token

param(
  [string]$OutFile = (Join-Path $PWD 'Dune-II-The-Building-of-a-Dynasty_DOS_EN-FR-DE.zip')
)
$ErrorActionPreference = 'Stop'

$gamePageUrl   = 'https://www.myabandonware.com/game/dune-ii-the-building-of-a-dynasty-1e7'
$downloadId    = 'lgxx-dune-ii-the-building-of-a-dynasty'   # EN-FR-DE / European version (4 MB)
# Other known IDs on the same page:
#   lgxw-dune-ii-the-building-of-a-dynasty   → 4 MB
#   lt0i-dune-ii-the-building-of-a-dynasty   → ISO 11 MB
#   p9ei-dune-ii-the-building-of-a-dynasty   → Disc Image 4 MB

Write-Host "1. Loading game page (establishes session)..."
$session = New-Object Microsoft.PowerShell.Commands.WebRequestSession
$null = Invoke-WebRequest -Uri $gamePageUrl -WebSession $session -UseBasicParsing

$downloadUrl = "https://www.myabandonware.com/download/$downloadId"
Write-Host "2. Requesting download endpoint: $downloadUrl"

# Follow redirects automatically; the final Location contains the tokenized URL
$null = Invoke-WebRequest -Uri $downloadUrl `
                              -WebSession $session `
                              -UseBasicParsing `
                              -MaximumRedirection 5 `
                              -OutFile $outFile

Write-Host "3. Saved to: $outFile"
Write-Host "   Size: $((Get-Item $outFile).Length) bytes"