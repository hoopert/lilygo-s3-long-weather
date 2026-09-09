#!/usr/bin/env bash
#
# Flash the Airstream Weather Panel onto a LilyGO T-Display-S3-Long.
#
# This is the command-line path. Most people should use the web installer
# instead - see docs/INSTALL.md - which needs nothing installed at all.
#
#   ./tools/flash.sh                       download the latest release and flash it
#   ./tools/flash.sh path/to/firmware.bin  flash a local image
#   PORT=/dev/ttyACM0 ./tools/flash.sh     force a specific serial port
#
# Requires Python 3. esptool is installed into a throwaway virtualenv so this
# does not touch your system packages.
#
set -euo pipefail

REPO="hoopert/lilygo-s3-long-weather"
IMAGE="${1:-}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

say() { printf '\033[36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[31mError:\033[0m %s\n' "$*" >&2; exit 1; }

command -v python3 >/dev/null || die "python3 is required."
command -v curl    >/dev/null || die "curl is required."

# --- the image -------------------------------------------------------------
if [[ -z "$IMAGE" ]]; then
  say "Fetching the latest release from $REPO"
  URL=$(python3 - "$REPO" <<'PY'
import json, sys, urllib.request
repo = sys.argv[1]
with urllib.request.urlopen(f"https://api.github.com/repos/{repo}/releases/latest") as r:
    data = json.load(r)
for asset in data.get("assets", []):
    if asset["name"].endswith(".bin"):
        print(asset["browser_download_url"])
        break
PY
  ) || die "Could not reach GitHub. Pass a local .bin instead: ./tools/flash.sh firmware.bin"
  [[ -n "$URL" ]] || die "No .bin asset on the latest release. Build one with 'cd firmware && pio run'."
  IMAGE="$WORK/airstream-weather.bin"
  curl -sSfL -o "$IMAGE" "$URL"
fi

[[ -f "$IMAGE" ]] || die "No such file: $IMAGE"
say "Image: $IMAGE ($(( $(wc -c < "$IMAGE") / 1024 )) KB)"

# --- esptool ---------------------------------------------------------------
say "Setting up esptool in a temporary virtualenv"
python3 -m venv "$WORK/venv"
"$WORK/venv/bin/pip" install --quiet --upgrade pip esptool
ESPTOOL="$WORK/venv/bin/esptool.py"

# --- port ------------------------------------------------------------------
# The board enumerates as Espressif's USB CDC device (303a:1001). Guessing is
# fine here because esptool will refuse to talk to anything that is not an
# ESP32-S3 anyway.
if [[ -z "${PORT:-}" ]]; then
  for candidate in /dev/ttyACM* /dev/ttyUSB* /dev/cu.usbmodem*; do
    [[ -e "$candidate" ]] && { PORT="$candidate"; break; }
  done
fi
[[ -n "${PORT:-}" ]] || die "No serial port found. Plug the board in, or set PORT=/dev/ttyACM0."
say "Port: $PORT"

# --- flash -----------------------------------------------------------------
say "Writing (the board resets itself when it is done)"
"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 "$IMAGE"

cat <<'EOF'

Done.

The panel will come up asking to be set up. On your phone, join the Wi-Fi
network "Airstream-Weather", and a setup page will open by itself. Pick your
network, save, and the forecast lands a few seconds later.

If the board did not respond: hold BOOT, tap RST, release BOOT, and run this
again. That forces it into download mode.
EOF
