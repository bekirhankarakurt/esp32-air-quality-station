# esp32-air-quality-station

ESP32 firmware (ESP-IDF, C++17) that measures temperature, humidity and CO₂, computes an air-quality index and streams CRC-protected telemetry over UDP. A host simulator and receiver let you run the whole pipeline on Linux without hardware.

[![CI](https://github.com/bekirhankarakurt/esp32-air-quality-station/actions/workflows/ci.yml/badge.svg)](https://github.com/bekirhankarakurt/esp32-air-quality-station/actions/workflows/ci.yml)
[![CodeQL](https://github.com/bekirhankarakurt/esp32-air-quality-station/actions/workflows/codeql.yml/badge.svg)](https://github.com/bekirhankarakurt/esp32-air-quality-station/actions/workflows/codeql.yml)
[![Coverage](https://img.shields.io/endpoint?url=https://bekirhankarakurt.github.io/esp32-air-quality-station/coverage/badge.json)](https://bekirhankarakurt.github.io/esp32-air-quality-station/coverage/)
[![Release](https://img.shields.io/github/v/release/bekirhankarakurt/esp32-air-quality-station)](https://github.com/bekirhankarakurt/esp32-air-quality-station/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3.1-E7352C.svg)
[**Documentation**](https://bekirhankarakurt.github.io/esp32-air-quality-station/)

![Simulator and receiver demo](docs/demo.gif)

## Why / what

- **Byte-accurate sensor drivers.** SHT31 and SCD41 over I2C, with a CRC-8 check on every 16-bit word, staged error recovery (soft reset, SCD41 restart, I2C bus reset) and SCD41 fallback when the SHT31 fails.
- **Signal processing on the device.** Moving-average filters, a 0–500 air-quality index built from CO₂, temperature and humidity sub-indices, comfort flags and dew point.
- **Telemetry built for links that lose data.** UDP with a binary format that shares its framing with [embedded-telemetry-daemon](https://github.com/bekirhankarakurt/embedded-telemetry-daemon) (magic `0x544C4D59`, big-endian, CRC-32 trailer) and an optional JSON line. The receiver reports lost and reordered packets.
- **Reliable firmware.** Wi-Fi reconnect with capped exponential backoff and jitter, config stored in NVS as a versioned blob with a CRC, a built-in web dashboard, no exceptions, and no heap use in the sampling loop.
- **Tested without hardware.** The portable core runs the same code on the host as on the device. An emulated I2C bus with fault injection drives it in dependency-free unit tests, under ASan/UBSan, and in the simulator.

## Demo

Simulator (real drivers + filters + AQI against emulated sensors in a simulated room, 1000× real time):

```text
$ ./build/aq_simulator --speed 1000 --count 6 --fault-rate 0.02
aq_simulator: binary -> 127.0.0.1:47800, period 5000 ms, speed x1000, seed 1
seq=0     t=     5s  22.35 C   46.8 %RH  dew  10.4 C  CO2   955 ppm  AQI  89 moderate  occ=2 window=closed faults=0
seq=1     t=    10s  22.38 C   46.8 %RH  dew  10.4 C  CO2   954 ppm  AQI  89 moderate  occ=2 window=closed faults=0
seq=2     t=    15s  22.41 C   46.9 %RH  dew  10.5 C  CO2   954 ppm  AQI  89 moderate  occ=3 window=closed faults=0
seq=3     t=    20s  22.42 C   47.0 %RH  dew  10.5 C  CO2   958 ppm  AQI  90 moderate  occ=3 window=closed faults=0
seq=4     t=    25s  22.42 C   47.0 %RH  dew  10.5 C  CO2   959 ppm  AQI  90 moderate  occ=3 window=closed faults=0
seq=5     t=    30s  22.42 C   47.1 %RH  dew  10.6 C  CO2   963 ppm  AQI  91 moderate  occ=3 window=closed faults=0
aq_simulator: 6 datagrams sent, 0 send errors, 0 injected faults, SHT31 errors 0 (recoveries 0), SCD41 errors 0 (recoveries 0)
```

Receiver, run at the same time:

```text
$ ./build/aq_receiver --count 6
aq_receiver: listening on udp/47800
fmt     seq   device    uptime  temp_C    rh_%   dew_C    co2  aqi level     flags  err
bin       0 0051A001        6s   22.35    46.8    10.4    955   89 moderate  TC-K   0
bin       1 0051A001       11s   22.38    46.8    10.4    954   89 moderate  TC-K   0
bin       2 0051A001       16s   22.41    46.9    10.5    954   89 moderate  TC-K   0
bin       3 0051A001       21s   22.42    47.0    10.5    958   90 moderate  TC-K   0
bin       4 0051A001       26s   22.42    47.0    10.5    959   90 moderate  TC-K   0
bin       5 0051A001       31s   22.42    47.1    10.6    963   91 moderate  TC-K   0
aq_receiver: 6 binary, 0 json valid; rejected: none; lost 0, reordered 0
```

Flags column: `T` = T/RH valid, `C` = CO₂ valid, `F` = SHT31 failed and SCD41 T/RH is used instead, `K` = comfortable.

## 60-second quick start

**1. Host simulator (no hardware).** You need CMake ≥ 3.16 and GCC ≥ 9 or Clang ≥ 10.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build -j
```

```bash
./build/aq_receiver &
```

```bash
./build/aq_simulator --format both --speed 100 --count 20
```

To stop the receiver, run `kill %1`. Each sample arrives twice, once as `bin` and once as `json`.

**2. Flash an ESP32.** Install [ESP-IDF v5.3](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32/get-started/), or use a prebuilt image from [Releases](https://github.com/bekirhankarakurt/esp32-air-quality-station/releases).

```bash
cd firmware
```

```bash
idf.py set-target esp32
```

```bash
idf.py menuconfig
```

In menuconfig, open *Air Quality Station* and set the Wi-Fi SSID and password, plus the IP of the PC that runs `aq_receiver`.

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

On the PC, run `./build/aq_receiver`. To see the dashboard, open `http://<station-ip>/`. The serial monitor prints the IP address.

To flash a release image without the toolchain (esp32; for esp32-s3 the bootloader offset is `0x0`):

```bash
esptool.py --chip esp32 -p /dev/ttyUSB0 write_flash 0x1000 aq-station-esp32-bootloader.bin 0x8000 aq-station-esp32-partition-table.bin 0x10000 aq-station-esp32-app.bin
```

## Hardware

| Part | Notes | Approx. price |
|---|---|---|
| ESP32-DevKitC (or ESP32-S3-DevKitC-1) | any ESP32 / ESP32-S3 module with 4 MB flash | $6–10 |
| Sensirion SCD41 breakout | photoacoustic CO₂ 400–5000 ppm ±(50 ppm + 5 %), also T/RH | $25–45 |
| Sensirion SHT31 breakout | ±0.2 °C / ±2 %RH reference T/RH | $3–8 |
| 2 × 4.7 kΩ resistors | external I2C pull-ups (most breakouts already have them) | <$1 |
| Breadboard, jumpers, USB cable | — | $5 |

### Wiring

Both sensors share one I2C bus at 100 kHz: SHT31 at `0x44`, SCD41 at `0x62`.

| Signal | ESP32 GPIO | ESP32-S3 GPIO | SHT31 | SCD41 |
|---|---|---|---|---|
| 3V3 | 3V3 | 3V3 | VIN | VDD |
| GND | GND | GND | GND | GND |
| SDA | 21 | 8 | SDA | SDA |
| SCL | 22 | 9 | SCL | SCL |
| ADDR (SHT31) | — | — | GND → `0x44` | — |

Keep the SCD41 away from the ESP32 module and the voltage regulator. Their heat biases the temperature reading, which is why the SHT31 is the primary T/RH sensor.

## Requirements

| Component | Version | Used for |
|---|---|---|
| CMake | ≥ 3.16 | host build |
| GCC / Clang | ≥ 9 / ≥ 10 (CI: GCC 13, Clang 18) | host library, simulator, receiver, tests |
| ESP-IDF | v5.3.1 (CI-pinned `espressif/idf:v5.3.1`) | firmware |
| Python + gcovr | optional | coverage report |
| cppcheck, clang-tidy, Doxygen | optional | static analysis, API docs |

## Build options

### Firmware (`idf.py menuconfig` → *Air Quality Station*)

| Kconfig option | Default | Description |
|---|---|---|
| `AQ_WIFI_SSID` | `myssid` | Wi-Fi network |
| `AQ_WIFI_PASSWORD` | `mypassword` | WPA2/WPA3 passphrase (empty = open network) |
| `AQ_TARGET_IP` | `192.168.1.100` | receiver IPv4; `x.x.x.255` broadcasts to the subnet |
| `AQ_TARGET_PORT` | `47800` | receiver UDP port |
| `AQ_SAMPLE_PERIOD_MS` | `5000` | sample/publish period, 1000–3 600 000 ms |
| `AQ_TELEMETRY_FORMAT` | binary | `binary`, `json` or `both` |
| `AQ_FILTER_WINDOW` | `6` | moving-average length, 1–32 samples |
| `AQ_ALTITUDE_M` | `0` | SCD41 altitude compensation, 0–3000 m |
| `AQ_I2C_SDA_GPIO` / `AQ_I2C_SCL_GPIO` | 21 / 22 (S3: 8 / 9) | I2C pins |
| `AQ_I2C_FREQ_HZ` | `100000` | I2C clock |
| `AQ_WEB_SERVER` | `y` | built-in dashboard on port 80 |

Target, port, period, format, altitude and window are **runtime** settings. Kconfig only provides their defaults. They can be changed from the web page (`POST /api/config`), are stored in NVS, and survive reboots. Wi-Fi credentials are build-time only and are never exposed over HTTP.

### Host (CMake)

| Option | Default | Description |
|---|---|---|
| `AQ_BUILD_TESTS` | `ON` | unit tests (CTest) |
| `AQ_BUILD_APPS` | `ON` | `aq_simulator`, `aq_receiver` |
| `AQ_WERROR` | `OFF` | warnings as errors (CI: `ON`) |
| `AQ_SANITIZE` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `AQ_COVERAGE` | `OFF` | gcov instrumentation |

### Host tools

| `aq_simulator` | Default | | `aq_receiver` | Default |
|---|---|---|---|---|
| `--target IP:PORT` | `127.0.0.1:47800` | | `--port N` | `47800` |
| `--format F` | `binary` | | `--csv FILE` | off |
| `--period-ms N` | `5000` | | `--count N` | unlimited |
| `--speed X` | `1` (real time) | | `--timeout-s N` | none |
| `--count N` / `--seed N` | unlimited / `1` | | `--quiet` | off |
| `--fault-rate P` | `0` (max 0.5) | | | |

## Telemetry format

### Binary packet (52 bytes, big-endian)

The header is identical to embedded-telemetry-daemon v1, so one listener can demultiplex both by message type.

| Offset | Size | Type | Field | Notes |
|---:|---:|---|---|---|
| 0 | 4 | u32 | magic | `0x544C4D59` = `"TLMY"` |
| 4 | 1 | u8 | version | `1` |
| 5 | 1 | u8 | message type | `2` = air quality (`1` = daemon system telemetry) |
| 6 | 2 | u16 | payload length | `24` |
| 8 | 8 | u64 | sequence | 0 after boot |
| 16 | 8 | u64 | timestamp | ns since boot (monotonic) |
| 24 | 4 | u32 | device id | low 32 bits of the Wi-Fi MAC |
| 28 | 4 | f32 | temperature | °C, filtered |
| 32 | 4 | f32 | humidity | %RH, filtered |
| 36 | 4 | f32 | dew point | °C |
| 40 | 2 | u16 | CO₂ | ppm, filtered |
| 42 | 2 | u16 | AQI | 0–500 |
| 44 | 1 | u8 | level | 0 good … 4 hazardous |
| 45 | 1 | u8 | flags | see below |
| 46 | 2 | u16 | error count | cumulative sensor errors, saturating |
| 48 | 4 | u32 | CRC-32 | IEEE 802.3 over bytes 0–47 |

Receivers reject packets with a wrong magic, version, type, length, CRC or level value. Fields whose valid flag is clear are undefined. The firmware writes NaN for them.

| Flag bit | Name | Meaning |
|---:|---|---|
| 0 | `THERMO_VALID` | temperature / humidity / dew point valid |
| 1 | `CO2_VALID` | CO₂ valid (a new SCD41 value arrived in the last 30 s) |
| 2 | `THERMO_FALLBACK` | SHT31 failing, T/RH come from the SCD41 (reads warm) |
| 3 | `COMFORTABLE` | 20–26 °C and 30–60 %RH |

### JSON line

```json
{"type":"air","seq":42,"ts_ns":215531000000,"device":"0051A001","temp_c":22.50,"rh_pct":41.25,"dew_c":8.75,"co2_ppm":845,"aqi":61,"level":"moderate","flags":11,"errors":0}
```

Invalid fields are written as `null`. On the device, `GET /api/live` returns the same object plus `rssi_dbm`, `wifi`, `udp_sent`, `udp_failed`, `wifi_reconnects` and `bus_recoveries`.

### Air-quality index

| Sub-index | Mapping |
|---|---|
| CO₂ | piecewise linear: 400→0, 800→50, 1000→100, 1500→150, 2500→200, 5000→300, 10000→500 ppm |
| Temperature | 0 inside 20–26 °C, +20 per °C outside, max 200 |
| Humidity | 0 inside 30–60 %RH, +2.5 per %RH outside, max 200 |

AQI = max(sub-indices). Levels: **good** 0–50 · **moderate** 51–100 · **poor** 101–150 (ventilate) · **unhealthy** 151–200 · **hazardous** > 200.

## Architecture

```text
                       ┌───────────────────────── lib/ (portable C++17, no heap, no exceptions) ──────────────────────────┐
  ┌──────────────┐     │  ┌───────────┐   ┌─────────────┐   ┌──────────────────────────┐   ┌───────────┐   ┌───────────┐ │
  │ EspI2cBus    │◄────┼──┤ Sht31     │   │             │   │ Station                  │   │ AQI /     │   │ telemetry │ │
  │ (i2c_master) │     │  │ Scd41     ├──►│ CRC-8 words ├──►│ recovery, plausibility,  ├──►│ dew point ├──►│ binary /  │ │
  └──────────────┘     │  └───────────┘   └─────────────┘   │ moving averages, fallback│   └───────────┘   │ JSON      │ │
  ┌──────────────┐     │        ▲  aq::I2cBus interface      └──────────────────────────┘                   └─────┬─────┘ │
  │ SimI2cBus    │◄────┼────────┘                                                                                 │       │
  │ (sim/, host) │     └──────────────────────────────────────────────────────────────────────────────────────────┼───────┘
  └──────────────┘                                                                                                 ▼
  firmware/main: WifiManager (backoff) · ConfigStore (NVS) · UdpPublisher ──── UDP ────► aq_receiver (validate, table, CSV)
                 WebServer (/, /api/live, /api/config) ◄── SharedState (mutex) ◄── sampling loop (vTaskDelayUntil)
```

### Design trade-offs

| Decision | Why | Cost |
|---|---|---|
| Hardware behind a 3-method `I2cBus` interface | identical driver code on the device, in tests and in the simulator; fault injection for free | one virtual call per transfer (negligible at 100 kHz) |
| Byte-level sensor emulation instead of mocking driver results | exercises framing, CRC, timing (NACK while converting) and state machines (SCD41 idle-only commands) | emulator code to maintain |
| Polling single-shot SHT31 + periodic SCD41 | SHT31 self-heating is minimal; SCD41 periodic mode is the accurate mode | 5 s CO₂ granularity |
| SHT31 primary, SCD41 T/RH only as fallback | SCD41 module self-heats by ~1–2 °C | flagged accuracy loss during fallback |
| Moving average with sum recomputed each push | no float drift over months of uptime | O(window) per sample (≤ 32 adds) |
| UDP, no acks; sequence numbers + CRC-32 | fire-and-forget is cheap for a sensor; loss is visible at the receiver | lost samples are not retransmitted |
| Fixed big-endian layout written byte by byte | portable across MCUs/hosts, no packing or aliasing UB | manual (de)serialisation, pinned by tests |
| Config as versioned CRC blob in NVS | atomic update, detects corruption, falls back to Kconfig defaults | layout change requires a version bump |
| No exceptions / RTTI; all buffers fixed-size | deterministic memory, smaller image; heap only used by IDF during init | explicit `Result` / error codes |

## Power and accuracy

- **Power (typical, 3.3 V).** ESP32 with Wi-Fi modem sleep is about 30–40 mA on average, with peaks near 250 mA during TX. The SCD41 in periodic mode draws about 15 mA on average (up to 175 mA peak). The SHT31 is negligible. Expect about 50–60 mA, or about 0.2 W. Use a USB supply; the station is not designed to run on batteries.
- **CO₂.** The SCD41 is accurate to ±(50 ppm + 5 % of reading). Automatic self-calibration assumes the room sees fresh air (about 400 ppm) at least once a week. Set `AQ_ALTITUDE_M` for your elevation. For example, Ankara is about 900 m; without compensation the reading there is about 10 % too low.
- **Temperature and humidity.** The SHT31 is accurate to ±0.2 °C and ±2 %RH. Mount it away from the ESP32 and the SCD41, ideally in free air on a short cable.
- **Filtering.** The default window of 6 samples at 5 s gives a 30 s moving average. Noise drops by about √6, and steps lag by about 15 s.

## Testing

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DAQ_SANITIZE=ON -DAQ_WERROR=ON
```

```bash
cmake --build build-asan -j
```

```bash
ctest --test-dir build-asan --output-on-failure
```

```bash
scripts/smoke_test.sh build-asan
```

| Test | Covers |
|---|---|
| `crc` | CRC-8 datasheet vector (0xBEEF→0x92), CRC-32 check value, every single-bit flip detected |
| `sensirion` | word framing round trip, CRC rejection, tick ↔ unit conversion endpoints |
| `drivers` | SHT31/SCD41 command bytes, delays, argument CRC, bus/CRC/range error propagation |
| `moving_average` | partial window, sliding, clamping, no drift after 10⁶ samples |
| `air_quality` | CO₂ breakpoints and monotonicity, comfort sub-indices, levels, dew point reference values |
| `telemetry` | binary round trip, header layout vs. daemon framing, every rejection path, every bit flip, exact JSON, malformed JSON |
| `config` | IPv4 parsing, limits, NVS blob corruption, URL-encoded form updates are atomic |
| `backoff` | doubling, cap, jitter bounds, overflow safety |
| `station` | nominal tracking vs. ground truth, CO₂ warm-up, missing SCD41, SHT31 fallback, 10 % fault injection, stale CO₂ |
| `sim` | deterministic RNG, one-week room physics, SHT31/SCD41 emulator state machines |

CI runs GCC and Clang builds with `-Werror`, an ASan/UBSan job, cppcheck and clang-tidy, a coverage job (fails below 90 % line coverage), CodeQL, and firmware builds for esp32 and esp32-s3 in `espressif/idf:v5.3.1`. Line coverage is about 97 %.

## Troubleshooting / FAQ

| Symptom | Fix |
|---|---|
| Receiver shows nothing | Check `AQ_TARGET_IP` or the web form, and that the host firewall allows UDP 47800 (`sudo ufw allow 47800/udp`). Use `x.x.x.255` to broadcast. |
| `aq_receiver: bind: Address already in use` | Another receiver is running; use `--port` or stop it. |
| CO₂ shows `--` for the first ~5 s | Normal: the SCD41 delivers its first value 5 s after start. |
| Flag `F` and temperature ~1.5 °C high | The SHT31 is not answering. Check its wiring and the ADDR pin (`0x44`); the SCD41 reading is used meanwhile. |
| `errors` keeps increasing | Long or unshielded I2C wires. Add 4.7 kΩ pull-ups, lower `AQ_I2C_FREQ_HZ` to 50000, keep the bus under 30 cm. |
| `wifi: disconnected (reason 201)` | SSID not found. Check the name; the ESP32 supports 2.4 GHz only. Retries back off up to 60 s. |
| CO₂ stuck high after days | SCD41 self-calibration needs regular fresh air; ventilate once a week. |
| `idf.py: command not found` | Run `. $IDF_PATH/export.sh`, or build in Docker: `docker run --rm -v $PWD:/project -w /project/firmware espressif/idf:v5.3.1 idf.py build`. |
| Web page settings rejected | The response text names the invalid field, e.g. `period must be 1000-3600000 ms`. |

## Project tree

```text
.
├── lib/                       portable core (host + ESP-IDF component)
│   ├── include/aq/            public headers: drivers, station, AQI, telemetry, config
│   └── src/                   implementation
├── sim/                       host-only room model + byte-accurate SHT31/SCD41 I2C emulation
├── apps/
│   ├── simulator/             aq_simulator: station pipeline on emulated sensors -> UDP
│   ├── receiver/              aq_receiver: validate, print, CSV log, loss statistics
│   └── common/                RAII UDP socket, CLI helpers
├── tests/                     dependency-free unit tests (CTest)
├── firmware/                  ESP-IDF project
│   ├── main/                  app, I2C bus, Wi-Fi manager, NVS store, UDP, web server, Kconfig
│   └── components/aqcore/     builds lib/ as an IDF component
├── scripts/smoke_test.sh      end-to-end simulator -> receiver check
├── docs/                      demo tape/GIF, Doxygen main page
└── .github/                   CI, CodeQL, Pages, release workflows; issue/PR templates
```

## Roadmap

- [x] SHT31 + SCD41 drivers with CRC and recovery
- [x] AQI, comfort and dew point
- [x] Binary (TLMY) + JSON UDP telemetry
- [x] NVS config, web dashboard, Wi-Fi backoff
- [x] Host simulator, receiver, CI, releases
- [ ] SGP41 VOC/NOx index
- [ ] Deep-sleep battery mode with SCD41 single-shot measurements
- [ ] OTA updates over HTTPS
- [ ] MQTT publisher option
- [ ] E-paper display

## License

[MIT](LICENSE). Bekirhan Karakurt - [github.com/bekirhankarakurt](https://github.com/bekirhankarakurt)
