# Praan AQ Node: Embedded Firmware Task

An ESP32-S3 sensor node that samples air quality at 1 Hz, logs every sample to SD first, and publishes to MQTT every 2 s, without losing data during network outages.

Planning board (requirements, decisions, progress): [Miro Board](https://miro.com/app/board/uXjVEcdE7Ys=/?share_link_id=224462705912)

| Deliverable | Where |
|---|---|
| 1. MCU selection + pin assignment | [below](#1-mcu-selection) |
| 2. Firmware flowchart + firmware | [`docs/flowcharts.pdf`](docs/flowcharts.pdf), [`firmware/`](firmware/), binary in [`firmware/release/`](firmware/release/) |
| 3. Wiring schematic | [`docs/schematic.pdf`](docs/schematic.pdf) |
| 4. MQTT packet format | [below](#4-mqtt-packet-format) |
| 5. Power budget | [below](#5-power-budget), [`docs/power_budget.pdf`](docs/power_budget.pdf) |
| 6. Hardware requirement sheet | [`docs/hardware_requirements.pdf`](docs/hardware_requirements.pdf) |

---

## 1. MCU selection

**ESP32-S3-WROOM-1U-N8** (8 MB flash, U.FL antenna)

- nRF parts were excluded: none have Wi-Fi and BLE on one chip, and main focus was ESP-IDF.
- ESP32-C3 and ESP32-C6 are single core and can only drive SD over SPI. The C3 is also tight on RAM for Wi-Fi + BLE + TLS.
- The S3 has an SDMMC host on any GPIO, 3 UARTs, native USB-JTAG (no USB-UART bridge needed), and dual core (Wi-Fi stack on core 0, application on core 1).
- Pre-certified module. The U.FL variant is for an external antenna.

| Peripheral | Pins |
|---|---|
| I2C bus (SHT30 0x44, BME280 0x76, DS3231 0x68) | SDA IO1, SCL IO2 |
| DS3231 1 Hz SQW (sampling tick) | IO4 |
| PMS5003 (UART1) | TX IO17, RX IO18, SET IO15, RESET IO16 |
| microSD (SDMMC 4-bit) | CLK IO12, CMD IO11, D0-D3 IO13/IO14/IO9/IO10, CD IO21 |
| RGB LED (LEDC) | IO5, IO6, IO7 |
| Button (BOOT + user) | IO0 |
| 5 V supply sense (divider not fitted) | IO8 |
| Power-good from supply (`PWR_GOOD`, active low) | IO38 |
| USB-JTAG | IO19, IO20 |

Pins avoided on purpose: strapping pins (IO3, IO45, IO46), IO33 to IO37 (kept free so an octal-PSRAM module fits without a layout change), and the JTAG pins (kept as a fallback).

## 2. Firmware

ESP-IDF v5.4 (v5.4-dev snapshot), C. Builds with zero warnings, and 58 host unit tests pass. Image is 1.25 MB, 40 % of the OTA slot. Flowcharts are in [`docs/flowcharts.pdf`](docs/flowcharts.pdf). Build and flash steps are in [`firmware/README.md`](firmware/README.md).

- **Store-first.** Every sample goes to SD, connected or not. The publisher reads from SD using two pointers: `read_idx` (acknowledged) and `send_idx` (next to send). Being offline only means the publisher falls behind. There is no separate offline code path.
- **Network loss.** Samples keep logging, and Wi-Fi reconnects with exponential backoff (1 to 60 s). On reconnect, the backlog drains in binary batches of 30 records while live data continues. Delivery is at-least-once, and sequence numbers let the backend deduplicate.
- **Storage.** Fixed 32-byte records with a CRC. They're written as 512 B sector-aligned blocks, so at most 16 s of data is lost on a power cut. 1 GB holds about 1 year of backlog. If the SD card fails, a RAM ring keeps the device running.
- **Timing.** The DS3231 SQW pin drives sampling and timestamps, so the ESP clock never drifts into the data. Each sampling cycle is designed to take under 100 ms of the 1 s period (to be confirmed on hardware).
- **Sensors.** SHT30 is the primary temperature/humidity source (±0.2 °C). The BME280 provides pressure, plus a temperature/humidity fallback and a drift check. A dead sensor sets a flag in the record and logging continues.

## 3. Wiring schematic

[`docs/schematic.pdf`](docs/schematic.pdf). MCU and peripherals only. 33 components. The count was kept low by using IO0 as both BOOT strap and user button, native USB instead of a bridge chip, and no RTC charge circuit.

## 4. MQTT packet format

Topic prefix `praan/v1/{device_id}/`, where `device_id` is `praan-` plus the last 3 bytes of the Wi-Fi MAC in hex (for example `praan-a1b2c3`). All multi-byte binary fields are little-endian.

| Topic | Direction | QoS | Retain | Payload |
|---|---|---|---|---|
| `telemetry` | device → server | 1 | no | JSON, 1–2 live samples every 2 s |
| `backlog` | device → server | 1 | no | Binary batch of up to 30 stored records |
| `status` | device → server | 1 | yes | JSON on every connect. LWT `{"online":false}` |
| `health` | device → server | 0 | no | JSON every 60 s |
| `cmd` | server → device | 1 | n/a | JSON command |
| `cmd/ack` | device → server | 1 | no | JSON result of each command |

QoS 1 is used for data, because losing samples is not acceptable and duplicates are handled by sequence number. Health uses QoS 0, because a missed health message is replaced 60 s later. Delivery is at-least-once, so the backend deduplicates on `(device_id, seq)`.

### Live telemetry (JSON)

```json
{"v":1,"s0":123456,"t0":1760169600,"d":[[0,2531,6512,10132,12,18,21,0],[1,2533,6510,10132,12,19,22,0]]}
```

| Field | Meaning |
|---|---|
| `v` | Format version, currently 1 |
| `s0` | Sequence number of the first row. Row *i* has `seq = s0 + i` |
| `t0` | UTC epoch seconds of the first row |
| `d` | Rows of `[dt, t_cdeg, rh_cpct, p_dhpa, pm1, pm25, pm10, flags]`. `dt` is seconds after `t0` |

Values are scaled integers, so the device does no float formatting. Units and invalid markers are the same as in the record below.

### Record (32 bytes, as stored on SD and sent in backlog)

| Offset | Type | Field | Unit / meaning |
|---|---|---|---|
| 0 | u32 | `ts` | UTC epoch s (uptime s if `TIME_INVALID`) |
| 4 | u16 | `flags` | Bit field, see below |
| 6 | i16 | `t_cdeg` | Temperature, 0.01 °C (SHT30, or BME280 on fallback) |
| 8 | u16 | `rh_cpct` | Relative humidity, 0.01 % |
| 10 | u16 | `p_dhpa` | Pressure, 0.1 hPa (BME280) |
| 12 | i16 | `bme_t_cdeg` | BME280 temperature, 0.01 °C (drift check) |
| 14 | u16 | `bme_rh_cpct` | BME280 humidity, 0.01 % (drift check) |
| 16, 18, 20 | u16 | `pm1`, `pm25`, `pm10` | Atmospheric PM, µg/m³ |
| 22, 24, 26, 28 | u16 | `n03`, `n05`, `n10`, `n25` | Particles > 0.3 / 0.5 / 1.0 / 2.5 µm per 0.1 L |
| 30 | u16 | `crc16` | CRC-16/CCITT-FALSE over bytes 0–29 |

An invalid value is `-32768` in a signed field or `65535` in an unsigned field, and the matching flag is set.

| Bit | Flag | Meaning |
|---|---|---|
| 0 | `TRH_FALLBACK` | T/RH came from the BME280 (SHT30 failed) |
| 1 | `TRH_INVALID` | No T/RH this second |
| 2 | `PRESS_INVALID` | No pressure this second |
| 3 | `PM_INVALID` | No valid PMS5003 frame |
| 4 | `PM_WARMUP` | PMS5003 within 30 s of start or reset; values may be low |
| 5 | `TIME_INVALID` | `ts` is uptime; the clock was not yet set |
| 6 | `TICK_FALLBACK` | Sampled on the internal timer (RTC square wave missing) |
| 7 | `SD_FALLBACK` | Record was held in RAM while the SD card was faulted |
| 8 | `BME_DIAG_INVALID` | No BME280 diagnostic values |

### Backlog batch (binary)

A 16-byte header followed by `count` raw records, at most 16 + 30 × 32 = 976 bytes.

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | char[4] | magic | `"PRN1"` |
| 4 | u8 | version | 1 |
| 5 | u8 | record size | 32 |
| 6 | u16 | count | 1–30 |
| 8 | u32 | seq0 | Sequence number of the first record. Record *i* has `seq = seq0 + i` |
| 12 | u32 | reserved | 0 |

Sequence numbers can have gaps between batches, for example padding written before a planned reboot, or records lost while the SD card was out. A gap is never filled later.

### Status, health and commands (JSON)

Status, retained, published on every connect:
```json
{"online":true,"fw":"1.0.0","reset":"poweron","boot":12,"unsent":0,"sd_free_mb":912,"rssi":-61}
```
`reset` is one of `poweron`, `external`, `software`, `panic`, `int_wdt`, `task_wdt`, `wdt`, `deepsleep`, `brownout`, `sdio`, `usb`, `jtag`, `unknown`.

Health, every 60 s:
```json
{"up":86400,"heap":158000,"heap_min":141000,"stack":{"sensor":1200,"storage":1500,"publish":1900,"led":1700},"unsent":0,"err":{"sht30":0,"bme280":0,"ds3231":0,"pms":3},"pms_resets":0,"dropped":0,"lost":0,"crc_err":0,"sd_err":0,"mqtt_reconn":1,"rtc_t":2875,"cycle_ms":61}
```
`stack` is the minimum free bytes seen per task, `rtc_t` is the DS3231 die temperature in 0.01 °C, and `cycle_ms` is the duration of the last sampling cycle. The other fields are counters since boot.

Commands on `cmd`, each answered on `cmd/ack`:

| Command | Example | Effect |
|---|---|---|
| `reboot` | `{"id":"c1","cmd":"reboot"}` | Flush SD, save `read_idx`, restart |
| `ota` | `{"id":"c2","cmd":"ota","url":"https://…/fw.bin"}` | HTTPS update, automatic rollback if the new image fails |
| `led` | `{"id":"c3","cmd":"led","enable":false}` | Status LED off (faults still shown), saved in NVS |
| `provision` | `{"id":"c4","cmd":"provision"}` | Reboot into BLE provisioning |
| `resync_time` | `{"id":"c5","cmd":"resync_time"}` | Force an SNTP sync |

```json
{"id":"c3","ok":true,"err":""}
```
`id` is up to 32 characters from `A–Z a–z 0–9 - _ . :`. On failure `ok` is `false` and `err` is one of `bad json`, `bad id`, `missing cmd`, `unknown command`, `url must be https://`, `ota already running`, `ota start failed`, `enable must be bool`, `nvs write failed`.

## 5. Power budget

| Rail | Average | Peak | Required (peak × 1.5) |
|---|---|---|---|
| 3.3 V | 42 mA | 468 mA | ≥ 800 mA |
| 5 V (PMS5003) | 100 mA | 150 mA | ≥ 300 mA |
| Motor (example 2× 12 V) | 1.0 A | 5.0 A | ≥ 7.5 A |

The controller draws 0.64 W. The PMS5003 accounts for 78 % of that, and duty-cycling it would save 62 %. No motor was specified, so the motor row is a labelled example.

## 6. Hardware requirements

[`docs/hardware_requirements.pdf`](docs/hardware_requirements.pdf) covers the block diagram, component selection and vendor power spec. One addition to the vendor spec: a power-good signal to IO38 with 50 ms of hold-up time. The firmware already handles it: a falling edge on IO38 flushes the pending SD block and saves `read_idx`, which removes the 16 s loss window. The pin has an internal pull-up, so it stays inert if the supply does not provide the signal.

---

## Additions beyond the brief

- RGB status LED with prioritized states (faults always win), for diagnosing problems in the field without a laptop
- BLE provisioning at boot only. BT memory is freed in normal operation.
- Button: short press for provisioning, 5 s for factory reset (SD data is kept)
- OTA with automatic rollback if the new image doesn't stay healthy for 60 s
- Health topic and remote commands

## Assumptions

- The node is mounted on the rover mentioned in the task. Controller load is the same in every motion mode.
- Wi-Fi only. No motor spec was given.

## Issues flagged

- DS3231 ZS-042 modules charge a non-rechargeable CR2032. The schematic uses the bare chip with no charge path.
- At continuous 1 Hz sampling, PMS5003 lifetime (MTTF about 3 years) and power are the limits. Duty-cycle it if the product allows.
- PMS5003 reads high above about 75 %RH. Correct for this on the backend using the SHT30 data.
- SHT31 is a drop-in upgrade for SHT30 (±2 %RH, no changes needed).

## Not yet verified on hardware

Everything builds and the logic is host-tested, but nothing has run on a board yet. Task stack sizes and power figures still need to be measured on hardware.