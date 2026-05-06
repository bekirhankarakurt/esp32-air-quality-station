#include "aq/sim/room.hpp"

#include <algorithm>
#include <cmath>

namespace aq::sim {

RoomModel::RoomModel(std::uint64_t seed) noexcept : rng_(seed) {}

void RoomModel::step(double dt_s) noexcept {
    while (dt_s > 0.0) {
        const double dt = std::min(dt_s, 1.0);
        substep(dt);
        dt_s -= dt;
    }
}

void RoomModel::substep(double dt) noexcept {
    constexpr double kTwoPi = 6.283185307179586;
    elapsed_s_ += dt;

    // Occupancy: people come and go on a ~30-40 minute time scale.
    if (occupants_ < 3 && rng_.chance(dt / 1800.0)) {
        ++occupants_;
    } else if (occupants_ > 0 && rng_.chance(dt / 2400.0)) {
        --occupants_;
    }

    // Ventilation: the window is opened when the air gets stale.
    if (window_left_s_ > 0.0) {
        window_left_s_ = std::max(0.0, window_left_s_ - dt);
    } else if (co2_ppm_ > kWindowOpenTriggerPpm && rng_.chance(dt / 300.0)) {
        window_left_s_ = kWindowOpenSeconds;
    }
    const double ach = window_open() ? kAchOpen : kAchClosed;

    // CO2 mass balance (ppm/s).
    const double generation = occupants_ * kCo2PerPersonLps / kVolumeLitres * 1e6;
    const double exchange = ach / 3600.0 * (co2_ppm_ - kOutdoorCo2Ppm);
    co2_ppm_ = std::max(kOutdoorCo2Ppm, co2_ppm_ + (generation - exchange) * dt);

    // Ornstein-Uhlenbeck drift terms (tau 15 min).
    const double decay = std::exp(-dt / 900.0);
    const double noise = std::sqrt(1.0 - decay * decay);
    temp_drift_ = temp_drift_ * decay + 0.25 * noise * rng_.normal();
    rh_drift_ = rh_drift_ * decay + 1.5 * noise * rng_.normal();

    // Temperature: relax toward a diurnal set point with occupant gains.
    const double diurnal = 1.2 * std::sin(kTwoPi * elapsed_s_ / 86400.0);
    const double set_point = 21.8 + diurnal + 0.3 * occupants_ - (window_open() ? 4.0 : 0.0) + temp_drift_;
    const double tau_t = window_open() ? 600.0 : 1800.0;
    temperature_c_ += (set_point - temperature_c_) * (1.0 - std::exp(-dt / tau_t));

    // Humidity: warmer air holds more water (RH drops), people add moisture.
    const double rh_target =
        std::clamp(46.0 + 2.5 * occupants_ - 2.0 * (temperature_c_ - 21.8) + rh_drift_, 15.0, 90.0);
    humidity_pct_ += (rh_target - humidity_pct_) * (1.0 - std::exp(-dt / 1200.0));
}

}  // namespace aq::sim
