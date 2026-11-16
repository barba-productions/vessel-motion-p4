# Vessel Motion P4 — ESP32-P4-NANO Firmware

Firmware for the **Waveshare ESP32-P4-NANO** board, built with the official
[Espressif ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/)
framework (no Arduino, no PlatformIO).

## Board hardware summary (per Waveshare wiki)

| Item | Detail |
|---|---|
| Main chip | ESP32-P4NRW32, dual-core RISC-V HP up to 360 MHz + LP core 40 MHz |
| PSRAM | 32 MB Hex-PSRAM stacked in package |
| Flash | 16 MB NOR flash via QSPI |
| Wireless | ESP32-C6-MINI-1 coprocessor (Wi-Fi 6 / BT 5) via SDIO |
| Ethernet | 100 Mbps RJ45, IP101GRI PHY via RMII |
| Display/Camera | MIPI-DSI 2-lane, MIPI-CSI with ISP |
| Audio | ES8311 codec + NS4150B amplifier |
| Storage | SDIO 3.0 TF card slot |

> Note: current chip revision is EC01 — USB Serial JTAG is **not** available;
> flashing/monitoring is done over the USB-C **UART** port. ADC is uncalibrated
> on this revision.

## Toolchain prerequisites

- ESP-IDF **v5.5.x** (stable). ESP32-P4 requires ESP-IDF ≥ v5.3.1 minimum;
  v5.5.x is the recommended stable line with full P4 support and is the line
  the Waveshare demos target.
- Build target: `esp32p4`

## Project structure (standard ESP-IDF)

```
├── CMakeLists.txt          # top-level project file
├── sdkconfig.defaults      # board defaults (target, 16 MB flash, 32 MB hex PSRAM)
├── main/
│   ├── CMakeLists.txt      # component registration
│   └── main.c              # minimal bring-up: chip/flash/PSRAM info + heartbeat
└── README.md
```

## Build

```powershell
idf.py set-target esp32p4   # once (also done automatically via sdkconfig.defaults)
idf.py build                # compile
idf.py -p COMx flash monitor  # later, after explicit approval
```

### PowerShell helpers

Run from PowerShell with execution policy allowed, or use
`powershell -NoProfile -ExecutionPolicy Bypass -File .\build_p4.ps1`.
All helpers resolve the project relative to their own location. Build/flash/release
share `p4_tools.ps1`, which reads the ESP-IDF version from `.vscode/settings.json`
(currently **5.5.5**) and loads the installed Espressif PowerShell profile. Override
the profile with `-IdfProfile 'C:\path\to\profile.ps1'` or `VESSEL_IDF_PROFILE`.

```powershell
.\build_p4.ps1                               # incremental build only
.\flash_p4.ps1 -Port COM3 -Baud 460800        # build, then full firmware flash
.\flash_p4.ps1 -Port COM3 -Monitor            # also open serial monitor afterward
.\upload_web.ps1 -BaseUrl http://<board-IP>   # web only: upload changed assets
.\upload_web.ps1 -BaseUrl http://<board-IP> -Files app.js,settings.html,settings-pi.js
.\stage_web.ps1 -Port COM3 -BaseUrl http://<board-IP> # build, flash, then web upload
```

Close any serial monitor before flashing. Scripts do not kill monitors or other
processes. Native command failures stop the workflow with a nonzero exit code;
build failure prevents flashing, and flash failure prevents uploading.
Web listing after flashing is optional: `-ListWeb -BaseUrl http://<board-IP>`.

**Browser assets are in internal LittleFS, not the SD card.** Uploads compare
SHA256 hashes and send only changed/missing files, then download and verify the
uploaded content. `-Files` selects paths relative to `web`; unchanged files are
still downloaded for comparison, so selecting files is fastest for UI iteration.
Without `-Files`, all local web assets are compared, including large models.
Non-destructive uploads leave obsolete remote assets in place. For a clean
initial deployment or to remove obsolete assets, explicitly use `-Format`
(also accepted by `stage_web.ps1`): this erases the **entire web partition**
before uploading all assets. It cannot be combined with `-Files`. SD logs and
saved settings are not formatted. A failed upload may leave a partial deployment;
rerun without `-Format` to retry. Hard-refresh the browser after deployment.

The uploader auto-detects the board at `http://192.168.11.222` (LAN) and
`http://192.168.4.1` (fallback AP). Override with `VESSEL_BASE_URL` if needed.

Connection checks print each attempt and stop after 30 seconds without formatting
anything if the board is unreachable. Use `-WaitSeconds 60` for a longer boot wait.
An explicit `-BaseUrl http://<board-IP>` (or `VESSEL_BASE_URL`) probes only that
address. A fresh board defaults to Ethernet DHCP, not `192.168.11.222`; a direct
PC-to-board cable needs a DHCP server or configured static addressing. Alternatively,
connect the PC's Wi-Fi to `Vessel-Motion` and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\upload_web.ps1 -BaseUrl http://192.168.4.1
```

`release_ota.ps1 -Version X.Y.Z` updates the centralized version, builds, packages,
and commits/pushes only the binary and manifest in the sibling
`vessel-motion-p4-OTA` clone. `-OtaRepo` overrides that location.
`-NoPush` packages locally without committing or pushing. `-SkipBuild` is only
for an existing binary whose embedded version matches the requested release;
it does not guarantee that all current source edits are included. Failed Git
commands are reported explicitly. A failed release may leave a version edit or
local packaged files/commit for inspection and retry.

Helper regression tests use an isolated mock IDF project and local HTTP server,
never the board or remote Git repositories (requires Node and the installed
5.5.5 Python environment):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\test\scripts_test.ps1
```

## Firmware configuration and UI iteration

[`main/globals.h`](main/globals.h) is the firmware configuration source. It
contains network/identity defaults, wireless policy, trip limits, algorithm
defaults, acquisition/telemetry/trip/overview rates, and vibration-strength
cutoffs. Defaults seed new/reset devices; **saved user settings override them**.
Changing these defaults does not reset an existing device's network settings.

### Impact threshold presets

Settings -> Motion provides a 1-5 Impact threshold slider. Higher levels count
only stronger movements; they are not vessel-size/speed classifications.
Tune vibration reduction first, then increase the threshold if ordinary motion
is counted, or decrease it if noticeable impacts are missed.

| Level | Description | Start (g) | End (g) |
|---|---|---:|---:|
| 1 | Very low | 0.10 | 0.06 |
| 2 | Low | 0.17 | 0.102 |
| 3 (default) | Standard | 0.25 | 0.15 |
| 4 | High | 0.40 | 0.24 |
| 5 | Very high | 0.60 | 0.36 |

`IMPACT_THRESHOLD_LEVELS_G` in [`main/globals.h`](main/globals.h) is the
firmware table of `{start_g, end_g}` pairs, sent to the browser as
`impactThresholdLevels` by `/api/settings`. Level 3 references
`DEFAULT_IMPACT_START_G` / `DEFAULT_IMPACT_END_G`, preserving the existing default.
Start begins an event; end is the lower threshold that must be held for the
configured end duration to finish it. Initially end is 60% of start. Both values
must be positive with end below start. Thresholds use gravity-removed,
fast-filtered resultant acceleration. Presets are provisional and need boat
validation; they do not change filter bandwidth, measured G, severity scaling,
or impact timing.

The numeric `impactStart` / `impactEnd` settings remain the persisted source of
truth. Existing settings are preserved: a pair not matching a preset displays
Custom. Select Custom to edit the exact pair under Advanced algorithm tuning;
switching back to Preset applies the slider's selected level. Changing the
firmware preset table never silently replaces saved pairs. Save applies edits
between trips only; both form and JSON API reject threshold changes during a
trip. Detection counts, logs and impact-related score contributions can change,
so trip scores collected with different thresholds are not directly comparable.
New trip `filterConfig` sidecars also record `impactStartG` and `impactEndG`;
older trips may lack these fields and are not rewritten.

### Other firmware defaults

- `DEFAULT_NETWORK_DHCP`: `1` for DHCP, `0` for fixed IPv4. Static defaults are
  `192.168.11.222/24`, gateway/DNS `192.168.11.1`.
- `WIRELESS_ALLOWED`: `0` blocks all Wi-Fi APIs, removes Wi-Fi settings/polling,
  omits hosted Wi-Fi/Bluetooth components, and holds the Nano's C6 reset control
  low on GPIO54. Saved credentials cannot override this policy.
- `MAX_TRIP_SECONDS` / `TRIP_RETENTION`: default eight hours / 100 completed trips.
  Duration automatically finishes/saves the trip; retention removes the oldest
  completed trips at boot and after trip completion. Retention supports 1-1000.
- `FIRMWARE_VERSION` / `FIRMWARE_PRODUCT`: used by CMake, health, asset versioning,
  and OTA. `release_ota.ps1` updates the version in this file only.
- Rates are coordinated firmware settings, not independent speed knobs. Changing
  acquisition rate also changes the one-second analysis window and calibration
  sample count. Revalidate IMU bandwidth/output rate, complementary-filter
  behavior, timing and scores before deploying a rate change.

**Radio-off limitation:** holding C6 reset from application startup does not
certify absence of emissions before startup, during reset, bootloader operation
or recovery. Verify the actual board revision/reset circuit and perform RF
testing. Installations requiring *never transmitting* may require an external
hardware reset/power interlock. Do not treat a hidden Wi-Fi tab or a successful
build as radio-compliance certification.

Wireless-enabled and disabled builds use different conditional dependencies.
The Component Manager may prune unused managed components when switching policy.
For production profiles, use separate local project checkouts outside OneDrive
to avoid dependency-cleanup locks and preserve each profile's build cache.
An explicit test override is supported: `idf.py -D WIRELESS_ALLOWED=0 build`.
Use a separate build/project for that override; it persists in its CMake cache.

### User settings

- **Graphics -> Attitude charts:** persisted symmetric Y limit, default
  -15 to +15 degrees, configurable 1-90. Applies to Overview, Trends and archived
  Trip History. It does not change measured or logged values.
- **Motion -> Engine vibration reduction:** disabled by default; remembers
  strength 1-5 (initially 3). Provisional strengths select 20, 15, 10, 7 and 5 Hz,
  capped at the normal fast cutoff so enabling reduction never increases it.
  Disabled uses the normal fast cutoff (initially 25 Hz). The 4 Hz slow/exposure
  path remains separate and unchanged.
- Filtering affects fast-path G values, impacts, related scoring and logged
  acceleration. Stop the trip before changing filters. Each new trip saves a
  `.filter.json` sidecar containing effective cutoffs, strength, sample rates and
  firmware identity. Trip detail JSON exposes `filterConfig`; legacy trips have
  `null` because their configuration was not recorded.
- Higher strength can suppress genuine wave/slamming peaks. Validate on the boat,
  choosing the lowest acceptable strength. This is manual tuning, not automatic
  engine calibration. Existing sensor-offset calibration remains separate.

The IMU acquisition defaults remain unchanged: CONFIG=0 and SMPLRT_DIV=0,
with the application reading at 200 Hz. For MPU-6050 this permits acceleration
bandwidth above the application's 100 Hz Nyquist limit; high-frequency vibration
can alias into slower apparent motion. Software reduction cannot undo aliasing.
`SENSOR_DLPF_CONFIG` and `SENSOR_SAMPLE_DIVIDER` are centralized for a separately
validated sensor-specific change; verify the detected IMU identity before tuning.
No unvalidated sensor-register change is silently enabled by the slider.

Trip History refreshes on a confirmed active-to-stopped transition, including
automatic/remote stops, and invalidates cached history on reconnection. Empty or
whitespace-only vessel names hide the entire name/separator element.

Deploy browser-only edits by uploading the changed assets and refreshing; these
do not need a firmware build. `upload_web.ps1 -Files` selects individual assets;
formatting is only performed with explicit `-Format`.

The Firmware settings page reads the installed version from the running image
on its initial status request, without pressing UPDATE or requiring internet.
`/api/ota/status` is not cached; before any update attempt it reports `idle`,
the installed version and "Ready to check for updates". UPDATE remains a
user-initiated network operation. OTA skips installation if the manifest
version equals the installed version, so reinstalling a corrected build with
the same version requires USB flashing.

### Focused regression checks

```powershell
node --test .\test\ui.test.js
```

With a host C compiler (for example `zig cc`), run the motion golden/filter test:

```powershell
zig cc -I.\main .\test\golden_test.c .\main\motion.c -o golden_test.exe
.\golden_test.exe
```

The settings host test uses small NVS/FreeRTOS stubs and ESP-IDF's actual cJSON
source; it tests defaults, saved-setting precedence, impact preset API values,
threshold-pair/range validation, form preflight and active-trip measurement locking:

```powershell
zig cc -I.\test\host_stubs -I.\main -I"$env:IDF_PATH\components\json\cJSON" `
  -include .\test\host_stubs\compat.h .\test\settings_test.c .\main\settings.c `
  .\main\motion.c "$env:IDF_PATH\components\json\cJSON\cJSON.c" -o settings_test.exe
.\settings_test.exe
```

The store test uses the same stubs and cJSON include flags. Compile
`test\store_test.c` with `main\motion.c` and cJSON (the test includes the store
implementation), then run in an empty temporary directory. It creates and
removes its own `trip-store-fixture` directory and checks exact retention,
incomplete-trip preservation, duration, filter metadata and complete CSV export.

The OTA host test reads the real OTA status implementation before any update,
checks idle defaults and preservation of update state, and aborts if a status
read attempts networking, task creation or flash writes:

```powershell
zig cc -I.\test\ota_stubs -I.\test\host_stubs -I.\main `
  -include .\test\host_stubs\compat.h .\test\ota_test.c -o ota_test.exe
.\ota_test.exe
```

Hardware acceptance still requires actual Ethernet/Wi-Fi/SD operation, saved
settings across updates, a shortened test-trip limit, retention rollover, and
boat recordings that compare engine vibration and genuine impacts.

## Preserved Waveshare pin maps (for later features)

### Ethernet — IP101GRI via RMII
| Signal | GPIO |
|---|---|
| TXD0 / TXD1 | 34 / 35 |
| RXD0 / RXD1 | 30 / 29 |
| TX_EN | 49 |
| CRS_DV | 28 |
| REF_CLK (50 MHz) | 50 |
| MDIO / MDC | 52 / 31 |
| PHY RESET | 51 |

### ESP32-C6 wireless coprocessor (SDIO)
Wi-Fi is provided by the C6 acting as slave. Add the components **after** the
bring-up build is verified:
```powershell
idf.py add-dependency "espressif/esp_wifi_remote"
idf.py add-dependency "espressif/esp_hosted"
```
The C6 is 2.4 GHz Wi-Fi 6 only.

### I2C (shared bus, ES8311 codec at 0x18)
SCL = GPIO8, SDA = GPIO7 (external pull-ups on board)

### SDMMC (TF card, 4-wire)
CLK = 43, CMD = 44, D0–D3 = 39–42

### I2S audio (ES8311 + NS4150B)
MCLK = 13, SCLK = 12, ASDOUT = 11, LRCK = 10, DSDIN = 9, PA enable = 53

## Roadmap

1. ✅ Minimal bring-up project (this commit) — must compile cleanly first
2. ⬜ Ethernet (IP101GRI) bring-up
3. ⬜ Wi-Fi via ESP32-C6 (`esp_hosted` + `esp_wifi_remote`)
4. ⬜ Application features (vessel motion sensing)

## References

- [Waveshare ESP32-P4-NANO wiki](https://www.waveshare.com/wiki/ESP32-P4-Nano-StartPage)
- [Schematic (PDF)](https://files.waveshare.com/wiki/ESP32-P4-NANO/ESP32-P4-NANO-schematic.pdf)
- [ESP-IDF ESP32-P4 docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/)
