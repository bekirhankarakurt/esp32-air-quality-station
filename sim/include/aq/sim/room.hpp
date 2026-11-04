/// @file room.hpp
/// @brief Physical model of a ventilated room with occupants (ground truth for the fake sensors).
#pragma once

#include <cstdint>

#include "aq/sim/rng.hpp"

namespace aq::sim {

/// Single-zone room: CO2 mass balance with occupant generation and air
/// exchange, temperature relaxing toward a diurnal set point, humidity coupled
/// to temperature and occupancy, plus Ornstein-Uhlenbeck drift on each.
class RoomModel {
public:
    struct Truth {
        double temperature_c;
        double humidity_pct;
        double co2_ppm;
    };

    static constexpr double kOutdoorCo2Ppm = 420.0;
    static constexpr double kVolumeLitres = 40'000.0;          ///< 40 m^3 bedroom / office.
    static constexpr double kCo2PerPersonLps = 0.0052;         ///< Sedentary adult exhalation.
    static constexpr double kAchClosed = 0.4;                  ///< Air changes per hour, window closed.
    static constexpr double kAchOpen = 6.0;                    ///< Window open.
    static constexpr double kWindowOpenTriggerPpm = 1400.0;    ///< Occupants open the window above this.
    static constexpr double kWindowOpenSeconds = 600.0;

    explicit RoomModel(std::uint64_t seed) noexcept;

    /// Advance the model by @p dt_s seconds (internally sub-stepped at <= 1 s).
    void step(double dt_s) noexcept;

    [[nodiscard]] Truth truth() const noexcept { return {temperature_c_, humidity_pct_, co2_ppm_}; }
    [[nodiscard]] int occupants() const noexcept { return occupants_; }
    [[nodiscard]] bool window_open() const noexcept { return window_left_s_ > 0.0; }
    [[nodiscard]] double elapsed_s() const noexcept { return elapsed_s_; }
    [[nodiscard]] Rng& rng() noexcept { return rng_; }

private:
    void substep(double dt) noexcept;

    Rng rng_;
    double elapsed_s_{0.0};
    double temperature_c_{22.4};
    double humidity_pct_{47.0};
    double co2_ppm_{960.0};
    double temp_drift_{0.0};
    double rh_drift_{0.0};
    double window_left_s_{0.0};
    int occupants_{2};
};

}  // namespace aq::sim
