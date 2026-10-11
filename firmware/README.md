# Praan AQ node firmware (ESP32-S3)

## 1. Overview
Samples SHT30, BME280 and PMS5003 once per second on the DS3231 1 Hz square wave and builds a 32-byte CRC-protected record.
Store-first: every record goes to a ring of segment files on the SD card before anything is sent. MQTT reads back from storage.
Delivery is at-least-once (QoS 1, resend from the last acknowledged record). The backend deduplicates on `(device_id, seq)`.

## 2. Hardware and pin map
ESP32-S3-WROOM-1U-N8 (8 MB flash, no PSRAM, U.FL antenna). Console and flashing over USB-Serial-JTAG.

| Peripheral | Interface | Address / port | Pins |
|---|---|---|---|
| SHT30 (T/RH primary) | I2C | 0x44 | SDA 1, SCL 2 |
| BME280 (pressure, T/RH fallback) | I2C | 0x76 | SDA 1, SCL 2 |
| DS3231 RTC, 1 Hz SQW timebase | I2C + GPIO IRQ | 0x68 | SQW 4 |
| PMS5003 | UART1 9600 8N1 | | TX 17, RX 18, SET 15, RESET 16 |
| microSD | SDMMC 4-bit | | CLK 12, CMD 11, D0 13, D1 14, D2 9, D3 10, CD 21 (active low) |
| RGB LED, common anode | LEDC | | R 5, G 6, B 7 |
| Button (BOOT strap) | GPIO | | 0 |
| Supply sense (divider not fitted) | ADC (reserved) | | 8 |
| Power-good from supply (`PWR_GOOD`), active low | GPIO IRQ | | 38 |

## 3. Build and flash
```sh
. $IDF_PATH/export.sh                 # ESP-IDF v5.3+ (built here with v5.4-dev)
idf.py set-target esp32s3
idf.py menuconfig                     # "Praan AQ Node": broker URI, user/password, PoP
idf.py build flash monitor
```
Prebuilt image: `release/flash.sh` writes `release/praan_aq_node_merged.bin` at 0x0 (set `PORT=/dev/...` if needed).
Host tests: `cmake -S test/host -B build-host && cmake --build build-host && ctest --test-dir build-host --output-on-failure`.

## 4. Provisioning
With no stored Wi-Fi credentials, the node advertises over BLE as `PRAAN-XXXXXX` (last 3 MAC bytes).
It uses security 1 with the PoP from `APP_PROV_POP`. Provision with the Espressif "ESP BLE Provisioning" app.
- **Short press (< 2 s):** reboot into provisioning. Existing credentials are kept if nobody provisions.
- **Long press (≥ 5 s, LED blinks white fast):** factory reset. Erases the Wi-Fi credentials and the `app` NVS namespace. SD data is kept.

## 5. Architecture
| Task | Core | Prio | Stack | Wakes on |
|---|---|---|---|---|
| sensor_task | 1 | 10 | 3 KB | SQW notify (1.5 s timeout, then esp_timer fallback) |
| storage_task | 1 | 8 | 4 KB | record queue (32) |
| publish_task | 0 | 6 | 5 KB | 2 s tick, PUBACK queue (8), connect/disconnect/command notify |
| led_task | 1 | 2 | 3 KB | 50 ms delay; also polls the button |

- **Store-first.** sensor → queue → storage. The publisher only ever reads storage, so a network outage never loses data.
- **Two pointers.** Everything before `read_idx` is acknowledged; `send_idx` is the next record to send. PUBACKs advance `read_idx` only over contiguous acked batches. `read_idx` is persisted every 15 acks, alternating between two CRC-checked meta slots.
- **16 s power-loss window.** Records are batched into one 512 B block (16 records) and written with one `write()` plus `fsync()`. Each boot resumes one block past the recovered end, so sequence numbers already published from RAM are never reused. If the supply provides power-good on IO38, a falling edge flushes the pending block and saves `read_idx` within the 50 ms hold-up, which closes this window.
- **Sector-aligned writes.** A 3600-record segment is exactly 225 sectors. Blocks never straddle sectors, which avoids FAT/SD read-modify-write. On boot, a torn tail is truncated and CRC-bad trailing records are dropped.
- **SQW timebase.** The timestamp counts DS3231 edges. It is re-anchored from the RTC every 600 s and after each SNTP sync, and the RTC is corrected when it is off by more than 1 s. If edges stop, a 1 Hz esp_timer takes over (`FLAG_TICK_FALLBACK`).

SD failure or card removal switches to a 300-record RAM ring (`FLAG_SD_FALLBACK`). The mount is retried every 30 s, and the ring is replayed to the card in order.
If the fault lasts more than 5 min, unsent records that exist only on the card are counted as lost (`records_lost` in `health`) and `read_idx` moves past them. No single fault stops publishing.
An NVS sequence floor keeps `seq` monotonic across reboots without a card.

## 6. MQTT
| Topic | Dir | QoS | Retain | Payload |
|---|---|---|---|---|
| `praan/v1/{id}/telemetry` | out | 1 | no | live JSON, 1–2 records |
| `praan/v1/{id}/backlog` | out | 1 | no | binary batch, up to 30 records |
| `praan/v1/{id}/status` | out | 1 | yes | JSON on connect; LWT `{"online":false}` |
| `praan/v1/{id}/health` | out | 0 | no | JSON every 60 s |
| `praan/v1/{id}/cmd` | in | 1 | | `{"id":"..","cmd":"reboot|ota|led|provision|resync_time",..}` |
| `praan/v1/{id}/cmd/ack` | out | 1 | no | `{"id":"..","ok":true,"err":""}` |

`{id}` is `praan-` plus the last 3 bytes of the STA MAC.
- **Telemetry.** `{"v":1,"s0":<seq>,"t0":<epoch>,"d":[[dt,t_cdeg,rh_cpct,p_dhpa,pm1,pm25,pm10,flags],..]}`. Row *i* has `seq = s0 + i`.
- **Backlog.** A 16 B little-endian header (`"PRN1"`, version 1, record size 32, count u16, seq0 u32, reserved u32), then `count` raw `record_t`. Sequence gaps (flush padding, records lost while the card was out) are skipped and never sent.

## 7. LED codes
| Priority | State | Colour | Pattern |
|---|---|---|---|
| 1 | SD fault | red | 4 Hz blink |
| 2 | sensor failing > 10 s | red | 2 blinks, 1 s pause |
| 3 | OTA | purple | breathing |
| 4 | provisioning | blue | 1 Hz blink |
| 5 | offline (was online) | amber | 1 Hz blink |
| 6 | connecting (never online since boot) | yellow | 4 Hz blink |
| 7 | online | green | 50 ms blip per acked publish |
| 0 | boot | white | solid until tasks start |

`{"cmd":"led","enable":false}` leaves only priorities 1–2 visible.

## 8. Known issues and recommendations
- ZS-042 DS3231 modules charge the CR2032 through a resistor and diode. Remove them, or put a bare DS3231 on the board.
- Pull-ups on several breakout modules sit in parallel and lower the effective I2C pull-up resistance.
- PMS5003 fan and laser MTTF is about 3 years at continuous 1 Hz. Duty-cycle it if the product allows.
- PMS5003 reads high above about 75 %RH. Apply humidity correction on the backend (T/RH are in every record).
- SHT31 is a drop-in upgrade (±2 %RH, no firmware change).
- BMP280 instead of BME280 saves cost but loses the humidity fallback and drift check. The driver rejects chip ID 0x58.
- Use an external antenna on a metal chassis.
- Re-provisioning needs a reboot, by design: BT memory (about 60 KB) is released at boot when not provisioning.
- A dead card costs its unsent backlog: after 5 min of SD fault, records that exist only on the card are counted in `records_lost` and skipped, so live publishing continues from RAM. The RAM ring holds 300 records (5 min), so a reboot while the card is still dead loses what the ring held.

## 9. Production checklist
- Enable Secure Boot v2 and flash encryption. Both are off in this dev config because they burn eFuses.
- Give each device a unique PoP, for example derived per unit at manufacturing.
- Set `CONFIG_LOG_DEFAULT_LEVEL_WARN`.
- Use a `mqtts://` broker with per-device credentials.

## 10. Verification status
**Done here:**
- `idf.py build` for esp32s3 has zero warnings with `-Wall -Wextra`. `-Wunused-parameter` and `-Wsign-compare` are re-enabled per component because ESP-IDF turns them off globally.
- The light-sleep variant (`APP_LIGHT_SLEEP=y`) also builds clean.
- 58 host tests pass. They cover CRC16 and CRC8 check values, `pms_parse`, BME280 compensation against the datasheet example, BCD/epoch, both encoders, ring math, meta slots, in-flight acks, backoff, button and LED logic.
- The merged image layout was checked.

**Needs real hardware:**
- I2C timing for all three sensors; the 20 ms trigger-to-read window.
- PMS5003 passive mode, reset and warm-up.
- SQW interrupt and the fallback/recovery switch.
- SD mount, power-cut recovery and card-swap replay.
- `fsync` latency and the free-space deletion path on a full 1 GB card.
- BLE provisioning flow.
- TLS to the real broker.
- OTA and rollback.
- Light sleep current.
- IO38 power-fail flush: confirm that the flush plus the meta write fit inside the supply hold-up time.
- The ROM CRC self-check on silicon (the software CRC is the fallback).
- Task stacks are the start values. Size them to about 1.5× the high-water marks reported in `health` after a soak test.
