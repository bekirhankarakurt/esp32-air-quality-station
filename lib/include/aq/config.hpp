/// @file config.hpp
/// @brief Runtime station configuration: validation, persistence blob and web-form parsing.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace aq {

/// Which encodings are published per sample.
enum class TelemetryFormat : std::uint8_t {
    Binary = 0,  ///< 52-byte TLMY packet.
    Json = 1,    ///< One JSON line.
    Both = 2,    ///< Two datagrams per sample.
};

[[nodiscard]] const char* to_string(TelemetryFormat f) noexcept;
[[nodiscard]] std::optional<TelemetryFormat> parse_format(std::string_view s) noexcept;

using Ipv4 = std::array<std::uint8_t, 4>;

/// Parse a dotted-quad IPv4 literal ("192.168.1.10"); no leading zeros or whitespace.
[[nodiscard]] std::optional<Ipv4> parse_ipv4(std::string_view s) noexcept;
/// Format @p ip into @p out (at least 16 bytes). Returns false if too small.
[[nodiscard]] bool format_ipv4(const Ipv4& ip, char* out, std::size_t capacity) noexcept;

/// Limits enforced by validate().
namespace limits {
inline constexpr std::uint32_t kMinPeriodMs = 1000;
inline constexpr std::uint32_t kMaxPeriodMs = 3'600'000;
inline constexpr std::uint8_t kMaxFilterWindow = 32;
inline constexpr std::uint16_t kMaxAltitudeM = 3000;  ///< SCD41 altitude compensation range.
}  // namespace limits

/// Settings that can be changed at runtime and are persisted in NVS.
/// Wi-Fi credentials are compile-time (menuconfig) and never stored here.
struct StationConfig {
    Ipv4 target_ip{192, 168, 1, 100};
    std::uint16_t target_port{47800};
    std::uint32_t sample_period_ms{5000};
    TelemetryFormat format{TelemetryFormat::Binary};
    std::uint16_t altitude_m{0};
    std::uint8_t filter_window{6};

    friend bool operator==(const StationConfig& a, const StationConfig& b) noexcept {
        return a.target_ip == b.target_ip && a.target_port == b.target_port &&
               a.sample_period_ms == b.sample_period_ms && a.format == b.format && a.altitude_m == b.altitude_m &&
               a.filter_window == b.filter_window;
    }
    friend bool operator!=(const StationConfig& a, const StationConfig& b) noexcept { return !(a == b); }
};

enum class ConfigError : std::uint8_t {
    None,
    BadAddress,
    BadPort,
    BadPeriod,
    BadFormat,
    BadAltitude,
    BadWindow,
    UnknownField,
};

[[nodiscard]] const char* to_string(ConfigError e) noexcept;
[[nodiscard]] ConfigError validate(const StationConfig& c) noexcept;

/// Versioned, CRC-protected persistence blob.
inline constexpr std::size_t kConfigBlobSize = 20;
using ConfigBlob = std::array<std::uint8_t, kConfigBlobSize>;
[[nodiscard]] ConfigBlob serialize(const StationConfig& c) noexcept;
/// std::nullopt if the size, version or CRC is wrong or the content does not validate.
[[nodiscard]] std::optional<StationConfig> deserialize(const std::uint8_t* data, std::size_t len) noexcept;

/// Apply an application/x-www-form-urlencoded body (ip, port, period_ms, format,
/// altitude_m, window) on top of @p base. Fields may be omitted. The result is
/// validated; on error @p base is left unchanged.
[[nodiscard]] ConfigError apply_form(std::string_view body, StationConfig& base) noexcept;

}  // namespace aq
