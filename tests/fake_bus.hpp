// Scripted I2C bus for driver tests: records writes, replays queued read frames.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include "aq/i2c_bus.hpp"

namespace aqtest {

class ScriptedBus final : public aq::I2cBus {
public:
    static constexpr std::size_t kMaxFrames = 8;
    static constexpr std::size_t kMaxBytes = 16;

    struct Frame {
        std::array<std::uint8_t, kMaxBytes> bytes{};
        std::size_t len{0};
        aq::I2cStatus status{aq::I2cStatus::Ok};
    };

    void queue_read(const std::uint8_t* data, std::size_t len, aq::I2cStatus st = aq::I2cStatus::Ok) {
        Frame& f = reads_[read_tail_++ % kMaxFrames];
        std::memcpy(f.bytes.data(), data, len);
        f.len = len;
        f.status = st;
    }

    aq::I2cStatus next_write_status{aq::I2cStatus::Ok};
    Frame last_write{};
    std::uint8_t last_address{0};
    std::uint32_t writes{0};
    std::uint32_t delayed_ms{0};

    [[nodiscard]] aq::I2cStatus write(std::uint8_t address, const std::uint8_t* data, std::size_t len) noexcept override {
        ++writes;
        last_address = address;
        std::memcpy(last_write.bytes.data(), data, len);
        last_write.len = len;
        const aq::I2cStatus st = next_write_status;
        next_write_status = aq::I2cStatus::Ok;
        return st;
    }

    [[nodiscard]] aq::I2cStatus read(std::uint8_t address, std::uint8_t* data, std::size_t len) noexcept override {
        last_address = address;
        if (read_head_ == read_tail_) {
            return aq::I2cStatus::Nack;
        }
        const Frame& f = reads_[read_head_++ % kMaxFrames];
        if (f.status != aq::I2cStatus::Ok) {
            return f.status;
        }
        std::memcpy(data, f.bytes.data(), len < f.len ? len : f.len);
        return aq::I2cStatus::Ok;
    }

    void delay_ms(std::uint32_t ms) noexcept override { delayed_ms += ms; }

    [[nodiscard]] std::uint16_t last_command() const noexcept {
        return static_cast<std::uint16_t>((last_write.bytes[0] << 8) | last_write.bytes[1]);
    }

private:
    std::array<Frame, kMaxFrames> reads_{};
    std::size_t read_head_{0};
    std::size_t read_tail_{0};
};

}  // namespace aqtest
