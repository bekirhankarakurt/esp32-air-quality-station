/// @file telemetry.hpp
/// @brief Air-quality telemetry record and its binary / JSON encodings.
///
/// The binary packet reuses the embedded-telemetry-daemon framing: a 24-byte
/// big-endian header with magic 0x544C4D59 ("TLMY"), version 1, a message type
/// and payload length, followed by the payload and an IEEE CRC-32 trailer.
/// Air-quality packets use message type 2.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "aq/air_quality.hpp"

namespace aq {

/// Bits of Reading::flags.
namespace flags {
inline constexpr std::uint8_t kThermoValid = 1U << 0U;     ///< temperature/humidity/dew point valid.
inline constexpr std::uint8_t kCo2Valid = 1U << 1U;        ///< co2_ppm valid.
inline constexpr std::uint8_t kThermoFallback = 1U << 2U;  ///< T/RH taken from the SCD41 (SHT31 failed).
inline constexpr std::uint8_t kComfortable = 1U << 3U;     ///< T and RH inside the comfort band.
}  // namespace flags

/// One published sample.
struct Reading {
    std::uint64_t seq{0};           ///< Sequence number, starts at 0 after boot.
    std::uint64_t timestamp_ns{0};  ///< Monotonic time since boot.
    std::uint32_t device_id{0};     ///< Low 32 bits of the station MAC address.
    float temperature_c{0.0F};      ///< Filtered temperature.
    float humidity_pct{0.0F};       ///< Filtered relative humidity.
    float dew_point_c{0.0F};
    std::uint16_t co2_ppm{0};       ///< Filtered CO2 concentration.
    std::uint16_t aqi{0};           ///< Air-quality index 0..500.
    AirLevel level{AirLevel::Good};
    std::uint8_t flags{0};
    std::uint16_t error_count{0};   ///< Cumulative sensor errors (saturating).

    [[nodiscard]] constexpr bool has(std::uint8_t flag) const noexcept { return (flags & flag) != 0U; }
};

/// Field-wise equality (floats compared bit-exactly, NaN == NaN).
[[nodiscard]] bool operator==(const Reading& a, const Reading& b) noexcept;
[[nodiscard]] inline bool operator!=(const Reading& a, const Reading& b) noexcept { return !(a == b); }

namespace wire {
inline constexpr std::uint32_t kMagic = 0x544C4D59U;  ///< ASCII "TLMY".
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::size_t kHeaderSize = 24;
inline constexpr std::size_t kPayloadSize = 24;
inline constexpr std::size_t kCrcSize = 4;
inline constexpr std::size_t kPacketSize = kHeaderSize + kPayloadSize + kCrcSize;
static_assert(kPacketSize == 52, "wire format size changed; bump kVersion");
}  // namespace wire

/// Message type in the shared header.
enum class MessageType : std::uint8_t {
    SystemTelemetry = 1,  ///< embedded-telemetry-daemon host metrics (not produced here).
    AirQuality = 2,       ///< This project.
};

/// Reasons a binary packet is rejected.
enum class DecodeError : std::uint8_t {
    None,
    TooShort,
    BadMagic,
    BadVersion,
    BadType,
    BadLength,
    BadCrc,
    BadValue,  ///< Well-formed packet with an out-of-range enum field.
};

[[nodiscard]] const char* to_string(DecodeError e) noexcept;

/// Result of decode_binary().
struct DecodeResult {
    DecodeError error{DecodeError::None};
    Reading reading{};
    [[nodiscard]] constexpr bool ok() const noexcept { return error == DecodeError::None; }
};

using Packet = std::array<std::uint8_t, wire::kPacketSize>;

/// Serialize into the binary wire format.
[[nodiscard]] Packet encode_binary(const Reading& r) noexcept;
/// Validate (magic, version, type, length, CRC) and decode a datagram.
[[nodiscard]] DecodeResult decode_binary(const std::uint8_t* data, std::size_t len) noexcept;

/// Buffer size sufficient for any encode_json() output including the terminator.
inline constexpr std::size_t kJsonBufferSize = 320;

/// Write a single-line JSON object terminated by '\n' into @p out.
/// Invalid fields (per flags) are written as null. Returns the length written
/// (excluding the NUL terminator), or 0 if @p capacity is too small.
[[nodiscard]] std::size_t encode_json(const Reading& r, char* out, std::size_t capacity) noexcept;
/// Parse a line produced by encode_json(). Returns std::nullopt on malformed input.
[[nodiscard]] std::optional<Reading> parse_json(std::string_view line) noexcept;

/// True if a datagram looks like JSON (first non-space byte is '{').
[[nodiscard]] bool looks_like_json(const std::uint8_t* data, std::size_t len) noexcept;

}  // namespace aq
