# Handoff: from a weather panel to Trailer Command

This repository ends at v1.3.3. Its successor is a trailer command system:
LilyGO T-Display S3 Long panels as MQTT thin clients, a Raspberry Pi 4 on
Venus OS Large as the brain (Victron VE.Direct devices, the MQTT broker,
Node-RED), and the weather app carried over as one screen among several.
This page records the recommendations made at the handoff so the first
session in the new repository starts from them rather than rediscovering
them.

## The three decisions

| Decision | Recommendation | The reason in one line |
|---|---|---|
| One repo or two | **One repository** for firmware, brain, simulator and contracts | The hard part is the contract between the halves; a topic rename must land in firmware, simulator, flow and dashboard in one change |
| Fork or import | **Import the history** (clone, add the new remote, push); do not use the GitHub Fork button | Keeps every commit and blame line without fork semantics (Actions and Issues off, the upstream link, sync nags) |
| Who routes the network | **A travel router, never the Pi** | Venus OS is an appliance image; OS updates rewrite everything outside `/data`, and routing on it turns every update into an outage |

Split into two repositories only if the Pi side becomes something other
people install on their own Venus units. That is a later decision and a
cheap one then.

## Repository shape

Keep this repository's firmware tree intact and add the rest beside it.

```
uc23-trailer-command/
  contracts/      topic map and JSON schemas: the single source of truth both sides import
  firmware/       this repo's firmware/ as it stands (PlatformIO, fonts, tools)
  brain/          Node-RED flows.json, brain/lib (plain JS modules with tests), dashboard static files, install scripts
  sim/            venus-sim (Python); CI also uses it to test the firmware's parsers on the host
  docs/           ARCHITECTURE.md, UX.md, STYLE.md, FLASH_CHECKLIST.md carried over and extended
```

What transfers as-is from this repository:

- The release automation: git-derived version (`tools/version.py`),
  automatic tagging on merge (`tools/next_version.py`, `[minor]` and
  `[major]` in a title), the firmware `.bin` attached to the release. Tag
  firmware and brain together so the Pi always knows which firmware matches
  it.
- The font pipeline (`tools/build_fonts.sh`) with generated fonts committed.
- The host-tested logic pattern (`tools/check_pressure_logic.py` in CI).
  Put the forecast maths in `brain/lib` the same way.
- The security rule: Wi-Fi credentials live in NVS on the device and never
  in the repository. A private repository does not relax this; repositories
  get made public later.

## A fleet of panels on one broker

Several panels on one broker fail in one specific way: two clients with the
same MQTT client ID kick each other off in a reconnect loop that looks like
flaky Wi-Fi. Design identity first.

| Concern | Recommendation |
|---|---|
| Identity | Client ID and hostname `panel-<role>-<mac4>`, e.g. `panel-galley-a1b2`. Role set in the existing captive portal (one more field), kept in NVS. The MAC suffix keeps two panels with the same role apart |
| Presence | Retained Last Will on `trailer/clients/<id>/status` (`online` / `offline`). Free with any real MQTT library; the Pi gets an honest roster with no polling |
| Telemetry | Retained heartbeat every 60 s on `trailer/clients/<id>/telemetry`: RSSI, free heap, free PSRAM, uptime, firmware version, IP, current screen, backlight level. A Node-RED "Fleet" tab lists every panel with last-seen and version |
| Role-driven config | The Pi publishes retained `trailer/clients/<id>/config` (screens to show, home position, night floor). A freshly flashed panel gets its job from the brain, not from a per-panel build |
| Fleet OTA | The Pi hosts the `.bin` on the LAN and publishes `trailer/fleet/firmware` `{version, url}`. Panels update over HTTP from the Pi; no panel needs the internet |
| Time | Panels set their clock by SNTP from the internet today. Publish `trailer/time` (epoch, retained, once a minute) from the Pi so clocks work with the WAN down |
| Subscriptions | Never wildcard `N/#`; subscribe per topic after portal discovery. Prefer the Pi-digested topics for anything beyond a dozen values |
| Library | espMqttClient, not PubSubClient. PubSubClient is synchronous with a 256-byte default buffer and blocks the loop LVGL shares. Hand values to the UI through the mutex-protected snapshot pattern `weather_snapshot()` already uses |
| Keepalive and heartbeat | Both on the one-second UI tick in `main.cpp`, never coupled to render or network callbacks |

Proposed topics the brain owns (the `N/<portal>/...` topics are Venus's):

| Topic | Retained | Payload |
|---|---|---|
| `trailer/clients/<id>/status` | yes | `online` / `offline` (Last Will) |
| `trailer/clients/<id>/telemetry` | yes | `{rssi, heap, psram, uptime, fw, ip, screen, bl}` |
| `trailer/clients/<id>/config` | yes | `{screens:[...], home:0, night_floor:16}` |
| `trailer/fleet/firmware` | yes | `{version, url, sha256}` |
| `trailer/time` | yes | `{epoch, offset}` once a minute |
| `trailer/weather/now`, `/hourly`, `/daily` | yes | Pre-digested `WxData` fields, a few KB, not the raw Open-Meteo body |
| `trailer/forecast/harvest/d1..d3`, `/soc/d1..d3` | yes | `{value, computed_at}` |

## Network and the Pi

| Layer | What | Why |
|---|---|---|
| WAN | Starlink in bypass mode, plus a USB or SIM modem as failover | Either source, same LAN behind it |
| Router | A small OpenWrt travel router (GL.iNet Beryl AX or Slate AX class) | Multi-WAN failover, VLANs, guest captive portal and vouchers (OpenNDS), happy on 12 V |
| Trailer VLAN | Pi with a static DHCP reservation, every panel, WLED nodes | The broker is plaintext on 1883 and must never be reachable from guests |
| Guest VLAN | Starlink resale users, isolated, rate-limited | Captive portal and billing are router features, not Pi features |

Consequences for the firmware: mDNS does not cross VLANs and is slow on the
ESP32, so `venus.local` is the first try and the static reservation in
config is the real path. Node-RED admin on port 1880 needs a password once
anyone else is on the LAN.

A non-technical flag: check the current Starlink service terms for the plan
in use before charging guests for access. Some plans restrict resale. That
is a policy question, not one this document can settle.

## If the new repository is private

- **GitHub Pages is not available on private repositories on the Free
  plan.** The web installer as built here (ESP Web Tools on Pages, the
  `.bin` fetched from the release) stops working. Two workable paths: serve
  the installer page and the `.bin` from the Pi on the trailer LAN, which is
  the fleet OTA host anyway; or keep a tiny public repository that only
  receives the manifest and `.bin` on each release. GitHub Pro enables Pages
  on a private repository, but the site itself is still public.
- **Release assets on a private repository need authentication to fetch.**
  A browser cannot pull them for an installer; the Pi can, with a token
  scoped to that repository, when it mirrors a release to the LAN.
- **Actions minutes are metered on private repositories** (2,000 per month
  on Free). A firmware build here runs about two minutes; the budget is
  fine, but keep docs-only changes from triggering full builds with path
  filters.
- Nothing changes about secrets: credentials stay in NVS and out of git.

## Corrections to the first prompt

Read before running the first session in the new repository.

1. **Palette conflict.** The prompt names this repository as the style
   authority and then specifies a different palette (charcoal `#4C4951`,
   gold `#E1B03C`, warm gray `#CCC5BF`, terracotta `#D19176`). The acceptance
   criterion "zero new colours outside tokens" cannot be met both ways.
   Decide first. A re-skin is cheap because every screen draws through
   `tokens.json` and the `COL_*` tokens in `theme.h`. What needs thought is
   semantics: turquoise means water and the active state, sunset means heat
   and alerts, sky means cold and wind. Gold as the primary data colour
   needs a mapping for those, not a swap.
2. **Phase 0 already exists.** `docs/ARCHITECTURE.md` documents the driver,
   LVGL 8.4 at 16-bit colour, the full-frame PSRAM buffer, the screen
   registry, gestures, fonts and an adding-a-screen walkthrough.
   `docs/UX.md` has the gesture map. `design/SPEC.md` and `tokens.json` are
   most of STYLE.md. Point the prompt at them and ask for a STYLE.md
   extraction only.
3. **It is Open-Meteo, not OpenWeather**, keyless already. The Pi proxy is
   still right, for a stronger reason than one fetch for all: the ESP32
   fetch is the fragile part of this app (TLS, a body up to 256 KB parsed in
   PSRAM). Have the Pi publish the pre-digested `WxData` fields so panels
   need neither TLS nor a large JSON parser. Keep the direct path as the
   fallback the prompt describes.
4. **Screen positions.** Screens live at integer positions on a strip, home
   is 0, and System is a gated drawer at −1. A Lights or Weather screen
   cannot also be at −1. Move System to a far position such as −9 (one
   constant) before adding screens. The registry holds eight.
5. **Venus keepalive.** An empty keepalive makes Venus republish every topic
   each time, a burst the ESP32 must absorb every 30 s. Send the first
   keepalive plain to get the snapshot, then
   `{"keepalive-options":["suppress-republish"]}` on the timer. Newer
   dbus-flashmq builds also accept a topic list in the keepalive payload to
   restrict what stays alive; verify on the installed Venus version with
   MQTT Explorer before relying on it.
6. **Node-RED persistence and testability.** Write the EWMA coefficient and
   history under `/data`; nothing else survives a Venus update. Put the
   forecast maths in `brain/lib` as a plain module with unit tests in CI,
   required from function nodes, not inside the flow JSON where it cannot be
   tested.
7. **Timer, not loop, for keepalive** is right, and the same rule applies to
   the heartbeat and to the staleness clock that greys values.

## Screens on the new strip

A sketch that respects the registry as it stands (positions unique, home at
0, drawers gated):

```
   -9 System (gated drawer)        0 Home            +1 Weather Today      +2 Forecast (10 days)
   -1 Lights (feature-flagged)     Solar | Airstream | Battery
                                   tap → Solar Detail   tap → Battery Detail (overlays, as Hour and Day Detail are)
```

Overlays stay on the top layer and open out of the tile that was tapped,
the way Hour Detail and Day Detail do, so the new pages inherit the
gesture rules and the Night Mode fade without new plumbing.
