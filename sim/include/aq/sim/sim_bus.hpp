/// @file sim_bus.hpp
/// @brief In-memory I2C bus with byte-accurate SHT31 / SCD41 emulation and fault injection.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "aq/i2c_bus.hpp"
#include "aq/sim/rng.hpp"
#include "aq/sim/room.hpp"

namespace aq::sim {

/// A target device on the simulated bus.
class SimDevice {
public:
    virtual ~SimDevice() = default;
    SimDevice(const SimDevice&) = delete;
    SimDevice& operator=(const SimDevice&) = delete;
    SimDevice(SimDevice&&) = delete;
    SimDevice& operator=(SimDevice&&) = delete;

    [[nodiscard]] virtual std::uint8_t address() const noexcept = 0;
    [[nodiscard]] virtual I2cStatus on_write(const std::uint8_t* data, std::size_t len, std::uint64_t now_ms) noexcept = 0;
    [[nodiscard]] virtual I2cStatus on_read(std::uint8_t* data, std::size_t len, std::uint64_t now_ms) noexcept = 0;

protected:
    SimDevice() = default;
};

/// I2C bus on a virtual millisecond clock. delay_ms() advances the clock
/// instantly, so the drivers run unmodified and simulations run faster than real time.
class SimI2cBus final : public I2cBus {
public:
    static constexpr std::size_t kMaxDevices = 4;

    explicit SimI2cBus(std::uint64_t seed) noexcept : rng_(seed) {}

    /// Attach a device; returns false if the bus is full. The device must outlive the bus.
    [[nodiscard]] bool attach(SimDevice& device) noexcept;
    /// Probability (0..1) that a transaction NACKs or a read returns a corrupted byte.
    void set_fault_rate(double p) noexcept { fault_rate_ = p; }
    void advance(std::uint64_t ms) noexcept { now_ms_ += ms; }

    [[nodiscard]] std::uint64_t now_ms() const noexcept { return now_ms_; }
    [[nodiscard]] std::uint32_t faults_injected() const noexcept { return faults_; }
    [[nodiscard]] std::uint32_t transactions() const noexcept { return transactions_; }

    [[nodiscard]] I2cStatus write(std::uint8_t address, const std::uint8_t* data, std::size_t len) noexcept override;
    [[nodiscard]] I2cStatus read(std::uint8_t address, std::uint8_t* data, std::size_t len) noexcept override;
    void delay_ms(std::uint32_t ms) noexcept override { now_ms_ += ms; }

private:
    [[nodiscard]] SimDevice* find(std::uint8_t address) const noexcept;

    Rng rng_;
    std::array<SimDevice*, kMaxDevices> devices_{};
    std::size_t device_count_{0};
    std::uint64_t now_ms_{0};
    double fault_rate_{0.0};
    std::uint32_t faults_{0};
    std::uint32_t transactions_{0};
};

/// SHT31 emulation: single-shot high-repeatability measurement and soft reset.
class SimSht31 final : public SimDevice {
public:
    explicit SimSht31(RoomModel& room) noexcept : room_(room) {}

    [[nodiscard]] std::uint8_t address() const noexcept override { return 0x44; }
    [[nodiscard]] I2cStatus on_write(const std::uint8_t* data, std::size_t len, std::uint64_t now_ms) noexcept override;
    [[nodiscard]] I2cStatus on_read(std::uint8_t* data, std::size_t len, std::uint64_t now_ms) noexcept override;

private:
    RoomModel& room_;
    std::array<std::uint8_t, 6> frame_{};
    std::uint64_t ready_at_ms_{0};
    bool pending_{false};
};

/// SCD41 emulation: periodic mode with a 5 s update interval, data-ready
/// status, stop / reinit / altitude commands with idle-state checks.
class SimScd41 final : public SimDevice {
public:
    explicit SimScd41(RoomModel& room) noexcept : room_(room) {}

    [[nodiscard]] std::uint8_t address() const noexcept override { return 0x62; }
    [[nodiscard]] I2cStatus on_write(const std::uint8_t* data, std::size_t len, std::uint64_t now_ms) noexcept override;
    [[nodiscard]] I2cStatus on_read(std::uint8_t* data, std::size_t len, std::uint64_t now_ms) noexcept override;

    [[nodiscard]] bool periodic() const noexcept { return periodic_; }
    [[nodiscard]] std::uint16_t altitude_m() const noexcept { return altitude_m_; }

private:
    enum class Pending : std::uint8_t { None, Measurement, DataReady };

    void update(std::uint64_t now_ms) noexcept;

    RoomModel& room_;
    std::array<std::uint8_t, 9> frame_{};
    Pending pending_{Pending::None};
    std::uint64_t next_sample_ms_{0};
    std::uint16_t altitude_m_{0};
    bool periodic_{false};
    bool ready_{false};
};

}  // namespace aq::sim
