# Flash the Airstream Weather Panel onto a LilyGO T-Display-S3-Long. (Windows)
#
# Most people should use the web installer instead - see docs/INSTALL.md - which
# needs nothing installed at all. This is the command-line path.
#
#   .\tools\flash.ps1                        download the latest release and flash it
#   .\tools\flash.ps1 -Image firmware.bin    flash a local image
#   .\tools\flash.ps1 -Port COM7             force a specific serial port
#
# Requires Python 3 on PATH. esptool goes into a throwaway virtualenv.

param(
    [string]$Image = "",
    [string]$Port  = ""
)

$ErrorActionPreference = "Stop"
$Repo = "hoopert/lilygo-s3-long-weather"
$Work = Join-Path ([System.IO.Path]::GetTempPath()) ("airstream-" + [guid]::NewGuid().ToString("N").Substring(0,8))
New-Item -ItemType Directory -Path $Work | Out-Null

function Say([string]$m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Die([string]$m) { Write-Host "Error: $m" -ForegroundColor Red; exit 1 }

try {
    if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        Die "Python 3 is required and was not found on PATH."
    }

    # --- the image ---------------------------------------------------------
    if ([string]::IsNullOrEmpty($Image)) {
        Say "Fetching the latest release from $Repo"
        try {
            $release = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest" `
                                         -Headers @{ "User-Agent" = "airstream-flash" }
        } catch {
            Die "Could not reach GitHub. Pass a local image: .\tools\flash.ps1 -Image firmware.bin"
        }
        $asset = $release.assets | Where-Object { $_.name -like "*.bin" } | Select-Object -First 1
        if (-not $asset) { Die "No .bin asset on the latest release." }
        $Image = Join-Path $Work "airstream-weather.bin"
        Invoke-WebRequest $asset.browser_download_url -OutFile $Image
    }

    if (-not (Test-Path $Image)) { Die "No such file: $Image" }
    Say ("Image: {0} ({1} KB)" -f $Image, [int]((Get-Item $Image).Length / 1024))

    # --- esptool -----------------------------------------------------------
    Say "Setting up esptool in a temporary virtualenv"
    python -m venv (Join-Path $Work "venv")
    $pip     = Join-Path $Work "venv\Scripts\pip.exe"
    $esptool = Join-Path $Work "venv\Scripts\esptool.py"
    & $pip install --quiet --upgrade pip esptool

    # --- port --------------------------------------------------------------
    if ([string]::IsNullOrEmpty($Port)) {
        $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
        if ($ports.Count -eq 0) {
            Die "No serial port found. Plug the board in, or pass -Port COM7."
        }
        # The board is usually the highest-numbered port to appear.
        $Port = $ports[-1]
    }
    Say "Port: $Port"

    # --- flash -------------------------------------------------------------
    Say "Writing (the board resets itself when it is done)"
    python $esptool --chip esp32s3 --port $Port --baud 921600 `
        write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB `
        0x0 $Image

    Write-Host ""
    Write-Host "Done." -ForegroundColor Green
    Write-Host @"

The panel will come up asking to be set up. On your phone, join the Wi-Fi
network "Airstream-Weather", and a setup page will open by itself. Pick your
network, save, and the forecast lands a few seconds later.

If the board did not respond: hold BOOT, tap RST, release BOOT, and run this
again. That forces it into download mode.
"@
}
finally {
    if (Test-Path $Work) { Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue }
}
