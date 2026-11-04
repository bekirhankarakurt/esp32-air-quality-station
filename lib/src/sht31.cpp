#include "aq/sht31.hpp"

namespace aq {
namespace sht31 {

Result<ThermoReading> parse(const Frame& frame) noexcept {
    const auto t = sensirion::decode_word(&frame[0]);
    const auto rh = sensirion::decode_word(&frame[3]);
    if (!t || !rh) {
        return {{}, SensorError::Crc};
    }
    return {{sensirion::ticks_to_celsius(*t), sensirion::ticks_to_rh(*rh)}, SensorError::None};
}

}  // namespace sht31

Result<ThermoReading> Sht31::measure() noexcept {
    if (const auto err = sensirion::send_command(bus_, address_, sht31::kCmdMeasureHigh); err != SensorError::None) {
        return {{}, err};
    }
    bus_.delay_ms(sht31::kMeasureTimeMs);
    sht31::Frame frame{};
    if (const auto st = bus_.read(address_, frame.data(), frame.size()); st != I2cStatus::Ok) {
        return {{}, sensirion::from_bus(st)};
    }
    return sht31::parse(frame);
}

SensorError Sht31::soft_reset() noexcept {
    const auto err = sensirion::send_command(bus_, address_, sht31::kCmdSoftReset);
    bus_.delay_ms(sht31::kResetTimeMs);
    return err;
}

}  // namespace aq
