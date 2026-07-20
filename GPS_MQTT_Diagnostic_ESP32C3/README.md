# GPS_MQTT_Diagnostic_ESP32C3

Publishes the same GPS diagnostic field set as
[`GPS_KitchenSink_FullFunctionTest_ESP32C3_v2`](../GPS_KitchenSink_FullFunctionTest_ESP32C3_v2/README.md)
as an MQTT JSON payload, once per second, so live GPS status can be viewed in Node-RED without a
laptop tethered to the serial port.

## Purpose

The kitchen-sink sketches dump full GPS status to the serial monitor. This sketch does the same
underlying read (via `TinyGPSPlus`) and the same MGRS conversion (via the
[`MGRS`](../MGRS/README.md) library), but publishes the result as JSON over Wi-Fi/MQTT instead —
useful for field testing the GPS module away from a computer, or for feeding a Node-RED dashboard.

## Hardware

- Seeed XIAO ESP32-C3
- Any NMEA-0183 GPS module (9600 baud UART output)

| GPS Module Pin | XIAO ESP32-C3 Pin |
|---|---|
| TX | GPIO20 / D7 |
| RX | GPIO21 / D6 |
| GND | GND |
| VCC | Per GPS module's supply voltage requirement |

## MQTT

| | |
|---|---|
| Data topic | `sensors/gps-tracker/diagnostic` |
| Status topic (LWT) | `sensors/gps-tracker/status` — retained `online` on connect, `offline` if the device drops off |
| Publish interval | 1000 ms |

Payload:

```json
{
  "fix": true,
  "lat": 38.897700,
  "lon": -77.036500,
  "mgrs": "18SUJ2339407395",
  "altitude_m": 45.2,
  "speed_mph": 0.10,
  "course_deg": 182.30,
  "satellites": 8,
  "hdop": 0.90,
  "date_utc": "2026-07-18",
  "time_utc": "14:32:07",
  "chars_processed": 48213,
  "chars_failed_checksum": 0,
  "fix_sentences": 203,
  "uptime_s": 3600
}
```

Before a fix is acquired, `fix` is `false` and the position/altitude/speed/course/date/time fields
hold zero or placeholder values — the diagnostic counters (`chars_processed`,
`chars_failed_checksum`, `fix_sentences`, `uptime_s`) are still meaningful for confirming the GPS
module is producing NMEA data at all.

## Installation

1. This sketch requires the [`MGRS`](../MGRS/README.md) library and the `PubSubClient` library
   (already used elsewhere in this vault — see the root `CLAUDE.md`).
2. Copy `secrets.h` and fill in your Wi-Fi SSID/password and MQTT broker host/port/user/password.
   `secrets.h` is covered by the project `.gitignore` — it is never committed.
3. The broker must already have a user provisioned for this device (this sketch does not create
   one). See "Broker setup" below.

```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 --library "../MGRS" GPS_MQTT_Diagnostic_ESP32C3
arduino-cli compile --upload -p COM3 --fqbn esp32:esp32:XIAO_ESP32C3 --library "../MGRS" GPS_MQTT_Diagnostic_ESP32C3
arduino-cli monitor -p COM3 -c baudrate=115200
```

## Broker Setup

The broker used here (`192.168.1.192:1883`) requires authentication — anonymous publish is not
enabled. Existing accounts (`axolotl-sensor`, `nodered`) are scoped to other devices, so this
sketch expects a dedicated `gps-tracker` user scoped to `sensors/gps-tracker/#`, matching the
vault's `sensors/<device>/<metric>` topic convention. Create it with `mosquitto_passwd` on the
broker host and add a matching ACL entry (mirroring however `axolotl-sensor`'s ACL is scoped)
before flashing this sketch.

## Operation

Serial output mirrors the exact JSON payload published to MQTT, once per second:

```
[MQTT] {"fix":true,"lat":38.897700,"lon":-77.036500,"mgrs":"18SUJ2339407395","altitude_m":45.2,"speed_mph":0.10,"course_deg":182.30,"satellites":8,"hdop":0.90,"date_utc":"2026-07-18","time_utc":"14:32:07","chars_processed":48213,"chars_failed_checksum":0,"fix_sentences":203,"uptime_s":3600}
```

A `WARNING: No GPS serial data detected.` message appears every 5 seconds if no NMEA data is
arriving from the GPS module at all. Wi-Fi and MQTT both reconnect automatically (non-blocking)
if the connection drops.

## Build Verification

Compiled clean against the `MGRS` and `PubSubClient` libraries (arduino-cli, `esp32:esp32:XIAO_ESP32C3`):

```
Sketch uses 978613 bytes (74%) of program storage space. Maximum is 1310720 bytes.
Global variables use 36776 bytes (11%) of dynamic memory, leaving 290904 bytes for local variables. Maximum is 327680 bytes.
```

(The larger program-storage footprint versus the serial-only kitchen-sink sketch is expected —
`WiFi.h` and `PubSubClient` pull in the ESP32 Wi-Fi/TLS stack.)

## Validation Results

MGRS conversion correctness is validated independently, with known-good control points, in
[`MGRS_Test`](../MGRS_Test/README.md) and [`MGRS_Validation`](../MGRS_Validation/README.md) —
since this sketch calls the same `latLonToMGRS()` library function, a passing result there is a
direct guarantee for this sketch's `mgrs` field as well. In the field, confirm the published
`mgrs` value is consistent with `lat`/`lon` by cross-checking against a known reference.

Confirmed running live end-to-end (2026-07-19): this sketch publishes continuously to the broker,
and the feed was independently verified two ways — an authenticated `mosquitto_sub` read
confirming the ACL scoping for both the `gps-tracker` (publish) and `gps-display` (read) accounts,
and the [`GPS_MQTT_Display_ESP32S3`](../GPS_MQTT_Display_ESP32S3/README.md) TFT dashboard
successfully rendering live GPS fixes from this feed.
