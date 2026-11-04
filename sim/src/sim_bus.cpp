#include "aq/sim/sim_bus.hpp"

#include <algorithm>
#include <cmath>

#include "aq/scd41.hpp"
#include "aq/sensirion.hpp"
#include "aq/sht31.hpp"

namespace aq::sim {
namespace {

std::uint16_t command_of(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) | data[1]);
}

}  // namespace

// ---- bus ------------------------------------------------------------------

bool SimI2cBus::attach(SimDevice& device) noexcept {
    if (device_count_ == devices_.size()) {
        return false;
    }
    devices_[device_count_++] = &device;
    return true;
}

SimDevice* SimI2cBus::find(std::uint8_t address) const noexcept {
    for (std::size_t i = 0; i < device_count_; ++i) {
        if (devices_[i]->address() == address) {
            return devices_[i];
        }
    }
    return nullptr;
}

I2cStatus SimI2cBus::write(std::uint8_t address, const std::uint8_t* data, std::size_t len) noexcept {
    ++transactions_;
    SimDevice* dev = find(address);
    if (dev == nullptr) {
        return I2cStatus::Nack;
    }
    if (fault_rate_ > 0.0 && rng_.chance(fault_rate_)) {
        ++faults_;
        return I2cStatus::Nack;
    }
    return dev->on_write(data, len, now_ms_);
}

I2cStatus SimI2cBus::read(std::uint8_t address, std::uint8_t* data, std::size_t len) noexcept {
    ++transactions_;
    SimDevice* dev = find(address);
    if (dev == nullptr) {
        return I2cStatus::Nack;
    }
    const I2cStatus st = dev->on_read(data, len, now_ms_);
    if (st == I2cStatus::Ok && len > 0 && fault_rate_ > 0.0 && rng_.chance(fault_rate_)) {
        ++faults_;
        // Flip one bit: emulates EMI on a long cable; the CRC-8 must catch it.
        const std::size_t byte = static_cast<std::size_t>(rng_.next_u64() % len);
        data[byte] = static_cast<std::uint8_t>(data[byte] ^ (1U << (rng_.next_u64() % 8U)));
    }
    return st;
}

// ---- SHT31 ----------------------------------------------------------------

I2cStatus SimSht31::on_write(const std::uint8_t* data, std::size_t len, std::uint64_t now_ms) noexcept {
    if (len != 2) {
        return I2cStatus::Nack;
    }
    switch (command_of(data)) {
        case sht31::kCmdMeasureHigh: {
            const auto truth = room_.truth();
            Rng& rng = room_.rng();
            const double t = truth.temperature_c + 0.04 * rng.normal();
            const double rh = std::clamp(truth.humidity_pct + 0.25 * rng.normal(), 0.0, 100.0);
            sensirion::encode_word(sensirion::celsius_to_ticks(static_cast<float>(t)), &frame_[0]);
            sensirion::encode_word(sensirion::rh_to_ticks(static_cast<float>(rh)), &frame_[3]);
            ready_at_ms_ = now_ms + 15;
            pending_ = true;
            return I2cStatus::Ok;
        }
        case sht31::kCmdSoftReset:
        case sht31::kCmdClearStatus:
            pending_ = false;
            return I2cStatus::Ok;
        default:
            return I2cStatus::Nack;
    }
}

I2cStatus SimSht31::on_read(std::uint8_t* data, std::size_t len, std::uint64_t now_ms) noexcept {
    // Without clock stretching the sensor NACKs reads until the conversion finishes.
    if (!pending_ || now_ms < ready_at_ms_ || len > frame_.size()) {
        return I2cStatus::Nack;
    }
    std::copy_n(frame_.begin(), len, data);
    pending_ = false;
    return I2cStatus::Ok;
}

// ---- SCD41 ----------------------------------------------------------------

void SimScd41::update(std::uint64_t now_ms) noexcept {
    while (periodic_ && now_ms >= next_sample_ms_) {
        const auto truth = room_.truth();
        Rng& rng = room_.rng();
        // Datasheet accuracy +-(50 ppm + 5 %); repeatability ~10 ppm.
        const double co2 = std::clamp(truth.co2_ppm + 8.0 * rng.normal(), 0.0, 40000.0);
        // Module self-heating: the on-board SHT4x reads warm and correspondingly dry.
        const double t = truth.temperature_c + 1.6 + 0.05 * rng.normal();
        const double rh = std::clamp(truth.humidity_pct * 0.9 + 0.3 * rng.normal(), 0.0, 100.0);
        sensirion::encode_word(static_cast<std::uint16_t>(std::lround(co2)), &frame_[0]);
        sensirion::encode_word(sensirion::celsius_to_ticks(static_cast<float>(t)), &frame_[3]);
        sensirion::encode_word(sensirion::rh_to_ticks(static_cast<float>(rh)), &frame_[6]);
        ready_ = true;
        next_sample_ms_ += scd41::kMeasurementIntervalMs;
    }
}

I2cStatus SimScd41::on_write(const std::uint8_t* data, std::size_t len, std::uint64_t now_ms) noexcept {
    update(now_ms);
    pending_ = Pending::None;
    if (len != 2 && len != 2 + sensirion::kWordSize) {
        return I2cStatus::Nack;
    }
    const std::uint16_t cmd = command_of(data);
    if (len == 2 + sensirion::kWordSize) {
        const auto arg = sensirion::decode_word(data + 2);
        if (cmd != scd41::kCmdSetAltitude || !arg || periodic_) {
            return I2cStatus::Nack;
        }
        altitude_m_ = *arg;
        return I2cStatus::Ok;
    }
    switch (cmd) {
        case scd41::kCmdStartPeriodic:
            if (periodic_) {
                return I2cStatus::Nack;
            }
            periodic_ = true;
            ready_ = false;
            next_sample_ms_ = now_ms + scd41::kMeasurementIntervalMs;
            return I2cStatus::Ok;
        case scd41::kCmdStopPeriodic:
            periodic_ = false;
            ready_ = false;
            return I2cStatus::Ok;
        case scd41::kCmdReinit:
            return periodic_ ? I2cStatus::Nack : I2cStatus::Ok;
        case scd41::kCmdReadMeasurement:
            pending_ = Pending::Measurement;
            return I2cStatus::Ok;
        case scd41::kCmdGetDataReady:
            pending_ = Pending::DataReady;
            return I2cStatus::Ok;
        default:
            return I2cStatus::Nack;
    }
}

I2cStatus SimScd41::on_read(std::uint8_t* data, std::size_t len, std::uint64_t now_ms) noexcept {
    update(now_ms);
    const Pending pending = pending_;
    pending_ = Pending::None;
    switch (pending) {
        case Pending::DataReady: {
            if (len != sensirion::kWordSize) {
                return I2cStatus::Nack;
            }
            sensirion::encode_word(ready_ ? 0x8006U : 0x8000U, data);
            return I2cStatus::Ok;
        }
        case Pending::Measurement:
            if (!ready_ || len != frame_.size()) {
                return I2cStatus::Nack;
            }
            std::copy(frame_.begin(), frame_.end(), data);
            ready_ = false;
            return I2cStatus::Ok;
        case Pending::None:
            break;
    }
    return I2cStatus::Nack;
}

}  // namespace aq::sim
