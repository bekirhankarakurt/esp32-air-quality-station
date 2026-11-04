# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 1.0.x | yes |
| < 1.0 | no |

## Reporting a vulnerability

Please report vulnerabilities privately through
[GitHub Security Advisories](https://github.com/bekirhankarakurt/esp32-air-quality-station/security/advisories/new).
Do not open a public issue. Include the affected component (firmware, receiver, simulator), the version,
steps to reproduce, and the impact you expect. You should get an acknowledgement within 7 days.

## Threat model and known limitations

The station is meant for a trusted home or lab LAN.

- UDP telemetry is not encrypted or authenticated. The CRC-32 detects corruption, not tampering.
- The web dashboard and `POST /api/config` use plain HTTP without authentication. Anyone on the LAN can
  change the telemetry target, period and format. Wi-Fi credentials are build-time only and are never
  readable or writable over HTTP.
- All network input (HTTP bodies, UDP datagrams in the receiver) is parsed with bounded, fixed-size buffers,
  and the parsers are covered by unit tests with malformed input.

Do not expose the station's HTTP port to the internet.
