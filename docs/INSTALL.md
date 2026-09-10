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

The panel comes up on a **GET STARTED** screen showing the name of its own
setup network, `Airstream-Weather`, an eight-digit password, and a QR code.

1. Point your phone's camera at the code and tap the prompt to join. Or join
   **`Airstream-Weather`** by hand and type the password off the screen. The
   network is local to the panel — it is not connected to anything.
2. A setup page opens by itself. (If it does not, browse to `192.168.4.1`.)
3. Choose your network, enter the password, and **Save**.

The setup password is generated on the panel's first boot and kept there; it
is not in this repository or in the firmware image, and it changes each time
you press **CHANGE NETWORK**.

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

Swipe to the **System** screen and press **CHANGE NETWORK**. It asks for a
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

Everything that produces the deliverable worked and only the deployment was
refused. Several unrelated causes produce this same symptom — a red deploy in
about two seconds under a green build — so read the failed job before guessing.

**The single most useful signal is whether the job ran any steps at all.**

#### The job shows *no steps*, not even "Set up job"

It was refused at the **environment gate**, before any action ran. Check:

> **Settings → Environments → `github-pages` → Deployment branches and tags**

GitHub creates that environment with a rule permitting only the default branch
*as it was at the time the environment was created*. Changing the repository's
default branch later does **not** rewrite that rule, so a deploy from `main` is
refused by a rule still naming some other branch. This is the one that bit this
repository.

Either allow all branches, or add `main`. Allowing all branches is safe here
because `.github/workflows/build.yml` already gates the deploy job with
`if: github.ref == 'refs/heads/main' || startsWith(github.ref, 'refs/tags/')` —
that `if:` is what restricts deployments, and it survives branch renames.

#### The job ran steps and one of them failed

Then it is the deployment itself. Check, in order:

1. **Source** is `GitHub Actions`, not "Deploy from a branch"
   (Settings → Pages → Build and deployment).
2. **`actions/configure-pages` runs before the artifact upload.** That step
   provisions the site and resolves its URL; the Source dropdown alone is not
   enough.
3. **Action versions** match what GitHub's own sample workflow currently emits
   (Settings → Pages → *Static HTML* → **Configure** prints one). A version that
   no longer resolves fails fast too.
4. **Permissions** include `pages: write` and `id-token: write`. They are set at
   the workflow level here and repeated on the deploy job.

Do **not** click **Configure** on either starter-workflow card on the Pages
settings page. Those generate a second workflow that competes for the same
deployment, and the Static HTML one publishes the whole repository root rather
than the installer. This project builds its own `site/` directory instead.

Jekyll is not involved at any point. Jekyll processing only applies to
branch-sourced Pages sites; an Actions-sourced deploy serves the uploaded
artifact verbatim, so there is nothing to disable and no `.nojekyll` needed.

### Versions

There is one version string and it comes from git. `tools/version.py` runs
`git describe --tags --always --dirty` and feeds the result to everything
that shows a version: the panel's System footer (injected as
`FIRMWARE_VERSION` when PlatformIO builds), the installer page's manifest,
and the release name. A tagged commit reads `1.1.0`; three commits past it
reads `1.1.0-3-gabc1234`; a local build with uncommitted changes adds
`-dirty`. Nothing is bumped by hand.

**Every merge to main is a release.** CI's `version` job runs
`tools/next_version.py`, which looks at the commits since the last tag and
bumps the patch number - or the minor if any commit (a PR title will do)
contains `[minor]`, or the major for `[major]` - tags the merge commit,
builds it, attaches the binary to a GitHub Release under that tag, and
republishes the installer page. Nobody types a number. `tools/flash.sh`
downloads the latest of those releases.

A tag pushed by hand (`git push origin v1.2.0`) still releases, for the rare
deliberate re-cut; the automatic job skips a commit that is already tagged.

---

## Troubleshooting

### Seeing what the panel is doing

Open the installer page with the board connected and press **Install firmware**
again. Once the board is already running this firmware the dialog offers
**Logs & Console** - the serial output, live in the browser. The first lines
after reset are the ones that matter:

```
[panel] readback RDDID=FFFFFFFF RDDPM=FF COLMOD=FF (advisory: reads are unverified on this glass)
[touch] controller: CST3530 @ 0x58
[flush] first frame (0,0)-(639,179)
[wx] located by IP: Los Angeles (33.955, -118.286)
[loop] up=5s bl=255/255 flushes=12 heap=132K lvmem=40% stack=6120
```

The `[panel] readback` line is the controller asked for its own state. On the
boards seen so far it answers `FF` to everything - the read opcode format is
unverified on this glass - so it is advisory only. A row of `FF` does not mean
the panel is dead; the splash does the real talking.

### Is it the hardware?

LilyGO publishes its own factory demo for this board as a single flashable
image, and it is the fastest way to separate "this firmware" from "this
board". It is the CST3530 build (the touch revision the panel reports at
boot):

```bash
curl -LO https://github.com/Xinyuan-LilyGO/T-Display-S3-Long/raw/master/firmware/factory-cst3530.bin
./tools/flash.sh factory-cst3530.bin
```

or, with no terminal, open <https://espressif.github.io/esptool-js/> in
Chrome, **Connect**, add the file at address `0x0`, **Program**, then press RST.

If the LilyGO demo shows its logo and clock, the glass, the backlight and the
QSPI wiring are fine and the fault is in this firmware - reflash it and send
the console output. If the LilyGO demo is black too, the board is faulty;
nothing in software will fix it.

| Symptom | Cause and fix |
|---|---|
| Board never appears as a serial port | Charge-only USB cable. Try another one first; this is by far the most common cause. |
| Install button greyed out or missing | Browser without Web Serial (Safari, Firefox). Use Chrome, Edge, or Opera. |
| Flash fails partway | Force download mode: hold BOOT, tap RST, release BOOT, retry. |
| Screen stays black | Read the console (see [above](#seeing-what-the-panel-is-doing)). Set `PANEL_BOOT_SELF_TEST` to 1 in `firmware/include/config.h` and rebuild: the panel is then driven directly, with no LVGL, for the first moment after power-on (top half turquoise, bottom half orange, backlight forced full). **If that splash shows** the driver, bus and backlight all work and the fault is LVGL-side - check for the `[flush]` line. **If it is black**, set `PANEL_BOOT_PROBE` to 1 as well and note which `[probe] step N` fills lit; that names the init sequence the glass wants. If nothing lights at all, flash LilyGO's own image (below): if that lights, the fault is this firmware's; if that is black too, the board is faulty. Every 5s until the first forecast lands, then once a minute, the console prints `[loop] up=... bl=... flushes=...`; if that never appears the main loop is hung. |
| Colors look lurid — reds and blues swapped | `LV_COLOR_16_SWAP` in `firmware/include/lv_conf.h`. It should be `1`. |
| Display is upside down | Swap `UI_ROTATION` in `firmware/include/config.h` between `LV_DISP_ROT_270` and `LV_DISP_ROT_90`. Leave `TOUCH_INVERT_X/Y` alone - LVGL rotates touch input with the display. |
| Taps land in the wrong place | Swipe to the System screen and watch the **TOUCH / RAW XY** readout while pressing each corner. Then flip `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` to match. |
| Touch does nothing at all | The System screen shows which controller was detected. `none detected` means neither the AXS15231B at `0x3B` nor the CST3530 at `0x58` answered on I2C — see [HARDWARE.md](HARDWARE.md). |
| Shows `NO CONNECTION` | Wi-Fi is up but the fetch failed. Check the serial log; the panel retries every 60 seconds on its own. |
| Forecast is for the wrong town | IP geolocation put you at your ISP's egress. Set latitude and longitude explicitly in the setup portal. |
