# Installing

Three paths, in order of how little you have to install first. Almost everyone
should use the first one.

---

## 1. One click, in a browser (recommended)

**Needs:** Chrome, Edge, or Opera on a desktop. Nothing else — no Python, no
PlatformIO, no drivers on any modern OS.

1. Connect the board to your computer with a USB-C cable. Use a cable you know
   carries data — a charge-only cable is the most common reason a board never
   shows up, and it looks identical.
2. Open the installer page (the GitHub Pages site for this repository; the URL
   is on the repo's front page under **Environments → github-pages**).
3. Press **Install firmware**, choose the serial port in the browser dialog, and
   confirm. It takes about 30 seconds.

If the port list is empty or the board does not respond, put it into download
mode by hand: **hold BOOT, tap RST, release BOOT**, then press Install again.

> Safari and Firefox do not implement Web Serial and never show the button.
> That is a browser limitation, not a problem with the board.

### Then: Wi-Fi

The panel comes up showing `JOIN WI-FI "Airstream-Weather" FROM YOUR PHONE`.

1. On your phone, join the **`Airstream-Weather`** network. It is open, and it
   is local to the panel — it is not connected to anything.
2. A setup page opens by itself. (If it does not, browse to `192.168.4.1`.)
3. Choose your network, enter the password, and **Save**.

**Leave the latitude and longitude fields blank.** The panel will locate itself
from its IP address and re-locate every time it moves, which is what you want in
a trailer. Fill them in only if you want the forecast pinned to a fixed place
regardless of where the panel is.

Set **Units** to `F` for Fahrenheit / mph / inches, or `C` for Celsius / km/h /
millimetres.

The panel reconnects, fetches a forecast, and is finished. Total time from
plugging in: under two minutes.

---

## 2. Command line

**Needs:** Python 3.

```bash
git clone https://github.com/hoopert/lilygo-s3-long-weather
cd lilygo-s3-long-weather
./tools/flash.sh
```

The script downloads the latest release binary, sets up `esptool` in a throwaway
virtualenv so it never touches your system packages, finds the serial port, and
flashes. To flash an image you built yourself, pass it as an argument. To force
a port, set `PORT`:

```bash
./tools/flash.sh firmware/.pio/build/airstream-weather/firmware.bin
PORT=/dev/ttyACM0 ./tools/flash.sh
```

On Windows, `.\tools\flash.ps1` does the same thing and takes `-Image` and
`-Port`.

On Linux you may need to be in the `dialout` group to open a serial port:

```bash
sudo usermod -aG dialout "$USER"   # then log out and back in
```

Wi-Fi setup afterwards is identical to path 1.

---

## 3. From source

**Needs:** [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/)
(`pip install platformio`).

```bash
cd firmware
pio run                      # build
pio run -t upload            # build and flash
pio run -t upload -t monitor # ...and watch the serial log
```

The first build downloads the ESP32 toolchain and Arduino core — roughly 500MB,
several minutes. Everything after that takes about 30 seconds.

Board support, partition layout and library pins are all in
[`firmware/platformio.ini`](../firmware/platformio.ini); the custom board
definition is in `firmware/boards/`. There is nothing to configure before the
first build.

### Over-the-air updates

Once the panel is on your network you never need the cable again:

```bash
pio run -t upload --upload-port airstream-weather.local
```

This matters more than usual for a device that is otherwise screwed to a
bulkhead behind a trim panel.

---

## Changing the Wi-Fi network later

Swipe to the **System** screen and press **CHANGE WI-FI NETWORK**. It asks for a
second tap to confirm — a stray touch on a wall-mounted screen should not take
the trailer's weather offline — and then reboots into the setup portal.

---

---

## Publishing the installer (maintainers)

The web installer is a GitHub Pages site built by `.github/workflows/build.yml`
on every push to `main`. It needs one repository setting that is **not** the
default:

> **Settings → Pages → Build and deployment → Source: _GitHub Actions_**

### If `build` is green but `deploy-pages` is red

The firmware compiled and the Pages artifact uploaded fine; only the deployment
was refused. `actions/deploy-pages` fails within a couple of seconds when there
is no Pages site for it to deploy into — it never gets as far as the artifact.

The workflow needs **`actions/configure-pages`** to run before the artifact is
uploaded. That step is what provisions the site and resolves its URL; setting
the Source dropdown to "GitHub Actions" by itself is not enough, and its absence
is easy to miss because the build job stays green and only the deploy goes red.

Check these in order — every one of them fails the same way, in about two
seconds, with the build job still green, so the symptom does not tell you which
you are looking at:

1. **The repository's default branch is `main`.** GitHub creates the
   `github-pages` environment with a deployment branch rule that only permits
   deployments from the *default branch*. If the default is still some other
   branch, a deploy triggered by a push to `main` is refused outright — the
   Source setting can be perfectly correct and it will still fail. Check
   Settings → General → Default branch.
2. **Source** is `GitHub Actions`, not "Deploy from a branch"
   (Settings → Pages → Build and deployment).
3. **`actions/configure-pages` runs before the artifact upload** — see above.
4. **Action versions** match what GitHub's own sample workflow currently emits.
   Settings → Pages → *Static HTML* → **Configure** prints one; it is also
   prefilled with the default branch name, which makes it a quick way to check
   item 1.
5. **Permissions** include `pages: write` and `id-token: write`. They are set at
   the workflow level here and repeated on the deploy job.

To cut a release that `tools/flash.sh` can download from:

```bash
git tag v1.0.0
git push origin v1.0.0
```

---

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| Board never appears as a serial port | Charge-only USB cable. Try another one first; this is by far the most common cause. |
| Install button greyed out or missing | Browser without Web Serial (Safari, Firefox). Use Chrome, Edge, or Opera. |
| Flash fails partway | Force download mode: hold BOOT, tap RST, release BOOT, retry. |
| Screen stays black | Give it ~2 seconds — the backlight deliberately fades in from dark rather than snapping on. If it stays black, check the serial log at 115200 baud for `could not allocate framebuffers`, which means PSRAM is not enabled. |
| Colors look lurid — reds and blues swapped | `LV_COLOR_16_SWAP` in `firmware/include/lv_conf.h`. It should be `1`. |
| Display is upside down | Change `UI_ROTATION` in `firmware/include/config.h` from `LV_DISP_ROT_90` to `LV_DISP_ROT_270`, and set both `TOUCH_INVERT_X` and `TOUCH_INVERT_Y` to `true`. |
| Taps land in the wrong place | Swipe to the System screen and watch the **TOUCH / RAW XY** readout while pressing each corner. Then flip `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` to match. |
| Touch does nothing at all | The System screen shows which controller was detected. `none detected` means neither the AXS15231B at `0x3B` nor the CST3530 at `0x58` answered on I2C — see [HARDWARE.md](HARDWARE.md). |
| Shows `NO CONNECTION` | Wi-Fi is up but the fetch failed. Check the serial log; the panel retries every 60 seconds on its own. |
| Forecast is for the wrong town | IP geolocation put you at your ISP's egress. Set latitude and longitude explicitly in the setup portal. |
