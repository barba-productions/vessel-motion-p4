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
