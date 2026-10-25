# Vessel Motion P4 — Change Log

Running log of changes applied to **Project 28 (Vessel Motion P4, ESP32-P4-Nano)**.
Intended to be passed as prompts/spec to the Vessel Motion Pi project. Keep this file
updated with every change session.

---

## 2026-10-24 — P4 owns its web assets (decoupled from the Pi project)

- Created `web/` inside the P4 project: full copy of the dashboard assets
  (index.html, app.css, app.js, settings.html, settings-pi.js, _header.html,
  header.css, large-type.css, fonts/, icons/, models/, vendor/, web2/, …).
- `stage_web.ps1` now stages from the P4's own `web/` folder (was: Project 27's
  web folder). `.vscode/tasks.json` cleaned of Pi-project paths.
- **Reason:** the P4 firmware serves dashboard files from the LittleFS `web`
  partition; they were previously uploaded from the Pi project, so the last Pi
  modifications (semi-circular 180° gauges with INSTANT/TRIP AVG/LAST HOURS tabs
  for MOTION COMFORT INDEX and RIDE SEVERITY INDEX) never reached the P4.

## 2026-10-24 — Acceleration chart legend

- `web/index.html`: ACCELERATION (G-FORCE) chart legend is now
  `LAT LONG VERT IMPACT` (added red impact marker to the legend).
- `web/app.css`: `.key.impact` renders a small red triangle (`--red`, #ff4d57)
  matching the chart's impact markers; fixed stale legend colors so
  LAT=cyan, LONG=orange, VERT=red (matching the actual uPlot series colors).

## 2026-10-24 — Build/flash scripts repaired (toolchain change)

- eim-cli and IDF v5.5.5 were removed from the machine; the project now builds
  with the classic ESP-IDF v5.5.3 install (`C:\esp\v5.5.3\esp-idf`,
  tools at `C:\Espressif\tools`).
- `build_p4.ps1`, `flash_p4.ps1`, `stage_web.ps1`: replaced eim-cli calls with
  `C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1` activation +
  direct `idf.py`. Flash port is COM3 (CH343 USB-serial).
- Stale bootloader CMake cache (pointing to v5.5.5) must be deleted once
  (`build/bootloader/CMakeCache.txt` + `CMakeFiles`) when switching IDF versions.

## 2026-10-24 — Settings page + firmware behavior pack (NOT yet built/flashed)

### Settings tabs
- Settings page now opens on the **VESSEL** tab (was NETWORK).
- The last active settings tab is remembered in `sessionStorage`
  (`vmSettingsTab`) and restored when returning from the dashboard pages.
- Files: `web/settings-pi.js`.

### New setting: RESET DATA WHEN TRIP STARTS (YES/NO)
- New persisted setting key **`resetOnTripStart`** (bool, default NO).
- When enabled, the `start` trip command also clears the live overview history
  and the live heat map, and bumps the telemetry reset sequence so connected
  dashboards clear their charts — the overview shows only data after trip start.
- UI: dropdown in Settings → DATA tab ("Trip data" card).
- Files: `main/settings.c` (DEFAULTS), `main/main.c` (vessel_command "start"),
  `web/settings.html` (DATA tab + bool fill list), `web/settings-pi.js`.

### Heat map runs continuously
- The live dashboard heat map now accumulates at RSI rate (10 Hz) with or
  without an active trip (was: only while a trip ran).
- Implementation: the per-trip heat map (saved into trip files) is a separate
  accumulator (`s_trip_heatmap`); `s_heatmap` is now the always-on live one,
  fed by new `store_heatmap_accumulate()`; cleared by the RESET command and by
  trip start when `resetOnTripStart` is on.
- Files: `main/store.c`, `main/store.h`, `main/main.c`.

### Network settings actually applied (Ethernet)
- `network.c` previously hardcoded 192.168.11.222/24 and always stopped DHCP.
  Now it honors the settings at boot: `dhcp=yes` → DHCP client; `dhcp=no` →
  static IPv4 from `ip`/`subnet`/`gateway`, plus `dns` (SNTP needs DNS) and
  `hostname`. Invalid static values fall back to 192.168.11.222/24 (link-safe).
- Boot order fixed in `app_main`: `nvs_flash_init` → `settings_init` →
  `time_init` run **before** `network_init` (settings must be loaded first).
  `store_init` no longer duplicates `nvs_flash_init`.
- Settings → NETWORK: the Static IPv4 fields are greyed out / disabled while
  DHCP = YES.
- Files: `main/network.c`, `main/main.c`, `web/settings-pi.js`.

### Trip retention 5 → 100
- `TRIP_RETENTION` is now **100** and retention is actually enforced (oldest
  trip `.meta`/`.bin` files are deleted when the limit is exceeded — previously
  retention was never implemented).
- `/api/trips` returns up to 100 trips and reports `"maximum":100`;
  trip-id discovery and listing now scan the SD directory instead of
  brute-forcing ids 1..999 (faster, no id-space limit).
- Dashboard label updated: "LAST 100 COMPLETED TRIPS".
- Files: `main/store.c`, `main/webserver.c`, `web/index.html`.

### Settings → DATA tab: logging memory
- `/api/storage` now also returns SD card logging capacity:
  `logs: { mounted, totalBytes, usedBytes, freeBytes }`
  (new `storage_sd_info()` via `esp_vfs_fat_info`).
- DATA → Storage card shows "LOGGING DATA x.x GB OF y.y GB USED (z%)" with a
  small meter bar.
- Files: `main/storage.c`, `main/storage.h`, `main/webserver.c`,
  `web/settings.html`, `web/settings-pi.js`.

### Time settings deep review — fixes
- **Root cause of "timezone not saving":** two stores held a timezone —
  `settings["timezone"]` (always "UTC", never written by the TIME tab) and the
  time manager's NVS `time/tz`. Every settings save called
  `time_set_timezone(settings_str("timezone"))` and clobbered the real timezone
  back to UTC.
- Fixes:
  - `PUT /api/time` now also writes the timezone into the settings store
    (both stores stay in sync).
  - The TIME tab timezone `<select>` now has `name="timezone"`, so the main
    settings form save also carries it (applied via `settings_applied()`).
  - `time_rtc_present()` returns **false** on the P4 (no battery RTC); TIME tab
    text no longer mentions the "Raspberry Pi RTC".
  - Trip labels are generated in the **configured local timezone** (were UTC).
  - The `ntp` form field (LAN NTP server) is now a real setting
    (`DEFAULTS["ntp"]`, default "" = pool.ntp.org) used when SNTP starts.
- Files: `main/webserver.c` (time_put), `main/time_mgr.c`, `main/store.c`,
  `main/settings.c`, `web/settings-pi.js`.

### Build fix (same session)
- `main/network.c`: replaced `IP4_ADDR(...)` fallback macros with
  `esp_netif_str_to_ip4(ETH_STATIC_*)` — network.c does not include the lwIP
  header that declares `IP4_ADDR` (implicit-declaration build error).

## 2026-10-24 — Fire button now deploys web assets too
- The ESP-IDF "fire button" (build+flash+monitor) only flashes firmware; the
  dashboard/settings web files live in the LittleFS `web` partition and were
  only updated by manual HTTP staging.
- New `upload_web.ps1`: waits for the board, formats the web partition, uploads
  all of `web/` over HTTP and verifies byte counts.
- `.vscode/settings.json`: added `idf.postFlashTask` so the fire button runs
  `upload_web.ps1` automatically after every flash. NOTE: the extension
  tokenizes the task string on spaces without honoring quotes, so the script
  must be referenced by its 8.3 short path
  (`C:\Users\barba\OneDrive\DOCUME~1\___ARD~1\PRAC37~1.VES\UPLOAD~1.PS1`)
  because the project path contains spaces.
- `stage_web.ps1` now delegates its upload phase to `upload_web.ps1`
  (single implementation).
- ESP-IDF extension port setting fixed: `idf.port` COM7 → COM3.

## 2026-10-24 — Settings save keeps the current tab
- Bug: clicking SAVE SETTINGS posted the form and the firmware's 303 redirect
  dropped the tab, so the page reopened on the NETWORK tab.
- Fix: the settings form already submits the visible tab in the hidden
  `activeTab` field; `settings_form_post` now redirects to
  `/settings#<tab>` (or `/advanced-settings#<tab>`), so the page reload lands
  back on the tab being edited. Tab slug sanitized to lowercase alnum.
  (The sessionStorage tab memory added earlier remains as a secondary path
  for navigation without saving.)
- File: `main/webserver.c`.
- Pi port note: `api.py save_settings_page` should redirect to
  `/settings#<activeTab>` using the posted `activeTab` form field.

## 2026-10-24 — Tab default/save-restore race fixed (settings.html inline script)
- Root cause of "opens on NETWORK" / "save returns to NETWORK": the inline
  script in `settings.html` auto-clicked the first regular tab (NETWORK) at
  parse time, which wrote `#network` into the URL *before* settings-pi.js read
  `location.hash` — defeating the VESSEL default, the sessionStorage restore,
  and the 303 `#tab` redirect.
- Fix: the inline script now only clicks a tab when the URL explicitly carries
  one (`#tab`), and only auto-defaults on `/advanced-settings` (which has no
  settings-pi.js). The regular page's initial/restore tab is owned entirely by
  settings-pi.js (VESSEL default, sessionStorage restore).
- Works with old firmware too: the save reload has no hash, so the
  sessionStorage restore path applies.
- File: `web/settings.html`.

## 2026-10-24 — Wi-Fi SEARCH found no networks
- Root cause: with no saved Wi-Fi credentials the board runs the fallback AP
  in pure `WIFI_MODE_AP`, and `esp_wifi_scan_start` fails in AP-only mode —
  the scan always returned 0 networks.
- Fix: `wifi_scan()` now creates the STA netif on demand and switches to
  APSTA (fallback AP keeps running) before starting the scan.
- File: `main/wifi_mgr.c`.

## 2026-10-24 — HELP + DISCLAIMER tabs in Settings
- New HELP tab ("Understanding your ride"): static educational page explaining
  MCI, RSI, gauge modes (INSTANT/TRIP AVG/LAST HOURS), the MCI/RSI comparison
  strip and the trip heat map (axes 0-10, real 7-stop palette, "% OF TRIP TIME"
  legend 0%→15%+, one red example cell bottom-left with callout, corner
  guides, "Red does not mean danger" clarification, hypothetical 25% example).
- New DISCLAIMER tab ("Model indications only") with the approved disclaimer
  text.
- All content is static; sample instruments are labeled "Illustrative
  examples — not live readings". Gauges replicate the dashboard semicircle SVG
  (same viewBox/gradient/marker formula), the heat map reuses the dashboard
  palette (helpHeatColor = app.js heatColor) drawn once per activation/resize
  (no continuous rendering). Graphics carry role="img" + aria-label; tab
  buttons get :focus-visible outlines.
- Wording verified against the algorithms: MCI is the exposure-integrating
  score (can rise/persist over time), RSI attacks fast and decays; TRIP AVG is
  a weighted trip summary, not an arithmetic average; heat map position =
  scores, color = % of trip time.
- File: `web/settings-pi.js` (tabs created dynamically, same convention as the
  VESSEL/TIME tabs; no firmware change). Tab persistence/restore works for
  both new tabs via the existing hash/sessionStorage logic.
- Pi port note: replicate the two tabs in the Pi settings page; the copy is
  device-agnostic except the heat map palette/gauge geometry which must match
  the Pi dashboard.

## 2026-10-24 — Corrections pack (Help layout, intro text, NTP rework)

1. **Help page layout fixes** (`web/settings-pi.js`)
   - The MCI/RSI comparison strip is now framed in a proper settings card
     (was an unstyled full-width strip floating between cards).
   - The rotated "MOTION COMFORT INDEX" axis label no longer overflows the
     heat map card (chart container margins adjusted). Centering uses
     `translate(-50%,-50%) rotate(-90deg)` — the initial
     `rotate(...) translateX(50%)` recipe pushed the label over the heading.
   - Note: `/api/time/sync` requires the new firmware — with old firmware the
     POST hits the wildcard static route and returns 405. Flash first.

2. **Settings intro removed** (`web/settings.html`)
   - Deleted the "System settings / Changes are stored on the vessel
     monitor..." intro block (and its JS reference).
   - "Network changes take effect after restart." moved into the NETWORK tab
     Connection card.

3. **TIME tab: SYNC TIME VIA INTERNET button**
   - Enabled only while Wi-Fi reports internet access (polled from
     `/api/wifi/status`, 5 s); greyed out with an explanatory tooltip otherwise.
   - Click → `POST /api/time/sync` → polls `/api/time` until `syncCount`
     increases (success) or ~12 s timeout; updates the local-time field.
   - Firmware: SNTP now uses three public servers with built-in fallback —
     `time.google.com`, `time.windows.com`, `time.cloudflare.com`.
     `time_sync_now()` forces an immediate re-query (`esp_sntp_restart`);
     `/api/time` gains `synced` + `syncCount` fields; `lastSync` reports
     SNTP/never.
   - Files: `main/time_mgr.c/h`, `main/webserver.c`, `web/settings-pi.js`.

4. **LAN NTP server setting removed**
   - The `ntp` field is gone from the NETWORK tab and from the firmware
     `DEFAULTS`; NTP is internet-only. (A stored `ntp` value in NVS is ignored
     by the defaults-merge; PATCHing `ntp` now correctly 400s.)
   - Files: `web/settings.html`, `main/settings.c`, `main/time_mgr.c`.

5. **RTC flag correction**
   - `time_rtc_present()` restored to `true` (the board has a rechargeable RTC
     battery per the original hardware note); TIME tab text updated to match.
   - Files: `main/time_mgr.c`, `web/settings-pi.js`.

- Pi port note: same four changes apply — intro removal + NETWORK note,
  internet-only NTP with the same three fallback servers + on-demand sync
  endpoint/button, drop the LAN NTP setting, and the Help-page card/label
  framing fixes.

## 2026-10-24 — TIME tab clarity + real timezone application fix

- **TIME tab** (`web/settings-pi.js`): the "Set date and time" field is now
  labeled "Local time in the selected timezone — UTC is calculated
  automatically" (was the confusing "ISO-8601 value including UTC offset"),
  and a live readout at the top of the card shows CURRENT UTC and CURRENT
  LOCAL (with the timezone name), ticking every second from `/api/time`.
  The firmware already interpreted the field as local time
  (`time_set_local_iso` → `mktime` under the configured TZ), so only the
  label/presentation changed.
- **Root-cause fix — timezone offsets never applied** (`main/time_mgr.c`):
  newlib's `tzset()` on ESP-IDF only understands POSIX TZ rules, not IANA
  names, so "America/New_York" silently fell back to UTC (local == UTC).
  `apply_tz()` now maps the curated IANA list to POSIX rules via `TZ_MAP`
  (e.g. America/New_York → `EST5EDT,M3.2.0/2,M11.1.0/1`); unknown values pass
  through only if they look like a raw POSIX rule, else UTC. The IANA name is
  still what is stored/displayed. Requires a firmware flash to take effect.
- Pi port note: the Pi has full zoneinfo so no mapping is needed there; only
  port the label/readout UI change.

## 2026-10-25 — Firmware v1.0.0, GitHub repos, OTA workflow

- **Version 1.0.0**: `CMakeLists.txt` → `project(vessel_motion_p4 VERSION 1.0.0)`
  (feeds `esp_app_get_description()->version`, the OTA "current version");
  `P4_VERSION` cache-bust string in `main/webserver.c` → "1.0.0".
- **GitHub**: main repo pushed to
  `github.com/barba-productions/vessel-motion-p4` (code + web assets);
  public OTA repo pushed to `github.com/barba-productions/vessel-motion-p4-OTA`
  (stable/manifest.json + firmware binary only).
- **OTA fixes** (`main/ota.c`): attached the x509 certificate bundle to the
  HTTPS client (was missing → TLS to GitHub would have failed); product string
  is now `vessel-motion-p4` (was `vessel-motion-pi`); the default manifest URL
  in `main/webserver.c` now points at the P4 OTA repo.
  Note: the manifest `sha256` is published but the firmware currently relies on
  `esp_ota_end()` image validation rather than comparing the manifest hash.
- **OTA release workflow**: `release_ota.ps1 -Version x.y.z` bumps the version,
  builds, packages `build/vessel_motion_p4.bin` into the OTA repo clone at
  `..\vessel-motion-p4-OTA\stable\`, regenerates the manifest, commits and
  pushes. OTA carries firmware only; web assets ship via `upload_web.ps1`.
- Pi port note: none — this is P4-specific infrastructure.

### Notes for the Pi port
- `storage.py DEFAULT_SETTINGS` needs: `resetOnTripStart: False`, `ntp: ""`.
- `api.py`: `/api/storage` gains the `logs` object; `/api/trips` maximum 100;
  `PUT /api/time` must sync `settings["timezone"]`.
- `service.py`: live heat map accumulates outside trips; trip start optionally
  resets live data when `resetOnTripStart` is set; trip retention 100 with
  real deletion.
- `network` layer: honor `dhcp`/`ip`/`subnet`/`gateway`/`dns`/`hostname` at boot.
- `settings.html`: VESSEL default tab + sessionStorage persistence,
  DATA tab "Trip data" card + storage meter, `resetOnTripStart` in the
  bool→yes/no fill list, timezone select gets `name="timezone"`.
