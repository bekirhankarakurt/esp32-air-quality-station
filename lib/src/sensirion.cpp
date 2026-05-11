#include "aq/sensirion.hpp"

#include <array>
#include <cmath>

namespace aq {

const char* to_string(SensorError e) noexcept {
    switch (e) {
        case SensorError::None: return "ok";
        case SensorError::Bus: return "bus error";
        case SensorError::Crc: return "CRC mismatch";
        case SensorError::NotReady: return "not ready";
        case SensorError::OutOfRange: return "out of range";
    }
    return "unknown";
}

namespace sensirion {
namespace {

std::uint16_t to_ticks(float scaled) noexcept {
    if (!(scaled > 0.0F)) {
        return 0;
    }
    if (scaled >= 65535.0F) {
        return 65535;
    }
    return static_cast<std::uint16_t>(std::lround(scaled));
}

}  // namespace

std::uint16_t celsius_to_ticks(float celsius) noexcept { return to_ticks((celsius + 45.0F) * 65535.0F / 175.0F); }

std::uint16_t rh_to_ticks(float rh) noexcept { return to_ticks(rh * 65535.0F / 100.0F); }

SensorError send_command(I2cBus& bus, std::uint8_t address, std::uint16_t command) noexcept {
    const std::array<std::uint8_t, 2> buf{static_cast<std::uint8_t>(command >> 8U),
                                          static_cast<std::uint8_t>(command & 0xFFU)};
    return from_bus(bus.write(address, buf.data(), buf.size()));
}

SensorError send_command(I2cBus& bus, std::uint8_t address, std::uint16_t command, std::uint16_t argument) noexcept {
    std::array<std::uint8_t, 2 + kWordSize> buf{};
    buf[0] = static_cast<std::uint8_t>(command >> 8U);
    buf[1] = static_cast<std::uint8_t>(command & 0xFFU);
    encode_word(argument, buf.data() + 2);
    return from_bus(bus.write(address, buf.data(), buf.size()));
}

}  // namespace sensirion
}  // namespace aq
