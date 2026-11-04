# esp32-air-quality-station API {#mainpage}

Portable C++17 core of the ESP32 air-quality station plus the host-side sensor simulation.

| Module | Header | Purpose |
|---|---|---|
| I2C abstraction | aq/i2c_bus.hpp | `aq::I2cBus` interface implemented by the firmware and the simulator |
| Sensor framing | aq/sensirion.hpp, aq/crc.hpp | CRC-8 word framing, unit conversion, `aq::Result` |
| Drivers | aq/sht31.hpp, aq/scd41.hpp | `aq::Sht31`, `aq::Scd41` |
| Pipeline | aq/station.hpp | `aq::Station`: recovery, filtering, fallback, evaluation |
| Evaluation | aq/air_quality.hpp, aq/moving_average.hpp | AQI, comfort, dew point, moving average |
| Telemetry | aq/telemetry.hpp | `aq::Reading`, binary TLMY packet, JSON line |
| Configuration | aq/config.hpp, aq/backoff.hpp | NVS blob, web-form parsing, reconnect backoff |
| Simulation | aq/sim/room.hpp, aq/sim/sim_bus.hpp | room physics, byte-accurate SHT31/SCD41 emulation, fault injection |

- [Unit-test coverage report](coverage/index.html)
- [Source on GitHub](https://github.com/bekirhankarakurt/esp32-air-quality-station)
