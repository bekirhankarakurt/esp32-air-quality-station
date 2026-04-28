#include "aq/scd41.hpp"

namespace aq {
namespace scd41 {

Result<Co2Reading> parse(const Frame& frame) noexcept {
    const auto co2 = sensirion::decode_word(&frame[0]);
    const auto t = sensirion::decode_word(&frame[3]);
    const auto rh = sensirion::decode_word(&frame[6]);
    if (!co2 || !t || !rh) {
        return {{}, SensorError::Crc};
    }
    if (*co2 > kMaxCo2Ppm) {
        return {{}, SensorError::OutOfRange};
    }
    return {{*co2, sensirion::ticks_to_celsius(*t), sensirion::ticks_to_rh(*rh)}, SensorError::None};
}

}  // namespace scd41

SensorError Scd41::start_periodic() noexcept {
    return sensirion::send_command(bus_, address_, scd41::kCmdStartPeriodic);
}

SensorError Scd41::stop_periodic() noexcept {
    const auto err = sensirion::send_command(bus_, address_, scd41::kCmdStopPeriodic);
    bus_.delay_ms(scd41::kStopTimeMs);
    return err;
}

SensorError Scd41::reinit() noexcept {
    const auto err = sensirion::send_command(bus_, address_, scd41::kCmdReinit);
    bus_.delay_ms(scd41::kReinitTimeMs);
    return err;
}

SensorError Scd41::set_altitude(std::uint16_t metres) noexcept {
    const auto err = sensirion::send_command(bus_, address_, scd41::kCmdSetAltitude, metres);
    bus_.delay_ms(scd41::kCommandTimeMs);
    return err;
}

Result<bool> Scd41::data_ready() noexcept {
    if (const auto err = sensirion::send_command(bus_, address_, scd41::kCmdGetDataReady); err != SensorError::None) {
        return {false, err};
    }
    bus_.delay_ms(scd41::kCommandTimeMs);
    std::array<std::uint8_t, sensirion::kWordSize> buf{};
    if (const auto st = bus_.read(address_, buf.data(), buf.size()); st != I2cStatus::Ok) {
        return {false, sensirion::from_bus(st)};
    }
    const auto word = sensirion::decode_word(buf.data());
    if (!word) {
        return {false, SensorError::Crc};
    }
    return {scd41::is_data_ready(*word), SensorError::None};
}

Result<Co2Reading> Scd41::read_measurement() noexcept {
    if (const auto err = sensirion::send_command(bus_, address_, scd41::kCmdReadMeasurement);
        err != SensorError::None) {
        return {{}, err};
    }
    bus_.delay_ms(scd41::kCommandTimeMs);
    scd41::Frame frame{};
    if (const auto st = bus_.read(address_, frame.data(), frame.size()); st != I2cStatus::Ok) {
        return {{}, sensirion::from_bus(st)};
    }
    return scd41::parse(frame);
}

}  // namespace aq
