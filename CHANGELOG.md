# Changelog

All notable changes to this project are documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-11-04

### Added

- SHT31 and SCD41 I2C drivers with CRC-8 validation of every word.
- Station pipeline with staged error recovery (SHT31 soft reset, SCD41 restart, I2C bus reset),
  SCD41 temperature/humidity fallback and stale-data detection.
- Moving-average filtering, 0-500 air-quality index, comfort flag and dew point.
- UDP telemetry: 52-byte binary packet using the embedded-telemetry-daemon framing
  (magic `0x544C4D59`, big-endian, CRC-32 trailer, message type 2) and JSON lines.
- ESP-IDF v5.3 firmware for ESP32 and ESP32-S3: Wi-Fi reconnect with exponential backoff,
  NVS-stored configuration, built-in web dashboard and configuration API.
- Host simulator with a physical room model and byte-accurate sensor emulation with fault injection.
- Host receiver with validation, CSV logging and loss/reordering statistics.
- Unit tests, sanitizer, static-analysis, coverage, CodeQL and firmware CI; API docs on GitHub Pages.

[Unreleased]: https://github.com/bekirhankarakurt/esp32-air-quality-station/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/bekirhankarakurt/esp32-air-quality-station/releases/tag/v1.0.0
