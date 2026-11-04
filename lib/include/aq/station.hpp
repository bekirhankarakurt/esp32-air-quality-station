/// @file station.hpp
/// @brief Sensor acquisition pipeline: drivers -> recovery -> filters -> AQI -> Reading.
#pragma once

#include <cstdint>

#include "aq/air_quality.hpp"
#include "aq/config.hpp"
#include "aq/i2c_bus.hpp"
#include "aq/moving_average.hpp"
#include "aq/scd41.hpp"
#include "aq/sht31.hpp"
#include "aq/telemetry.hpp"

namespace aq {

/// Static parameters of a station instance.
struct StationSettings {
    std::uint32_t device_id{0};
    std::uint8_t filter_window{6};
    std::uint16_t altitude_m{0};
    ComfortThresholds comfort{};
};

/// Per-sensor error accounting.
struct SensorHealth {
    std::uint32_t errors{0};       ///< Total failed operations.
    std::uint32_t consecutive{0};  ///< Failures since the last success.
    std::uint32_t recoveries{0};   ///< Recovery sequences issued.
    SensorError last_error{SensorError::None};
};

/// Owns both drivers and turns raw measurements into published readings.
///
/// Error handling: a failed SHT31 read keeps the last filtered value for up to
/// kRecoveryThreshold - 1 samples, then a soft reset is issued and the SCD41's
/// temperature / humidity are used as a flagged fallback. A failing SCD41 is
/// stopped, re-initialised and restarted. CO2 is reported invalid once no new
/// value has arrived for kCo2StaleNs.
class Station {
public:
    static constexpr std::uint32_t kRecoveryThreshold = 3;
    static constexpr std::uint64_t kCo2StaleNs = 30'000'000'000ULL;
    static constexpr float kMinTempC = -40.0F;
    static constexpr float kMaxTempC = 125.0F;

    Station(I2cBus& bus, const StationSettings& settings) noexcept;

    /// Configure the SCD41 (altitude) and start periodic measurement.
    [[nodiscard]] SensorError begin() noexcept;
    /// Apply new settings: clears filters and restarts the SCD41.
    [[nodiscard]] SensorError reconfigure(const StationSettings& settings) noexcept;
    /// Acquire, filter and evaluate one sample. Never blocks longer than the
    /// sensors' conversion times (plus 530 ms during an SCD41 recovery).
    [[nodiscard]] Reading sample(std::uint64_t timestamp_ns) noexcept;

    [[nodiscard]] const SensorHealth& sht31_health() const noexcept { return sht_health_; }
    [[nodiscard]] const SensorHealth& scd41_health() const noexcept { return scd_health_; }
    [[nodiscard]] const StationSettings& settings() const noexcept { return settings_; }

private:
    void sample_thermo() noexcept;
    void sample_co2(std::uint64_t timestamp_ns) noexcept;
    [[nodiscard]] SensorError start_scd41() noexcept;
    static void record_ok(SensorHealth& h) noexcept;
    static void record_error(SensorHealth& h, SensorError e) noexcept;

    Sht31 sht_;
    Scd41 scd_;
    StationSettings settings_;
    MovingAverage<limits::kMaxFilterWindow> temp_avg_;
    MovingAverage<limits::kMaxFilterWindow> rh_avg_;
    MovingAverage<limits::kMaxFilterWindow> co2_avg_;
    SensorHealth sht_health_{};
    SensorHealth scd_health_{};
    Co2Reading last_scd_{};
    bool have_scd_{false};
    bool scd_running_{false};
    std::uint64_t last_co2_ns_{0};
    std::uint64_t seq_{0};
};

}  // namespace aq
