#include "aq/station.hpp"

#include <cmath>

namespace aq {

Station::Station(I2cBus& bus, const StationSettings& settings) noexcept
    : sht_(bus), scd_(bus), settings_(settings), temp_avg_(settings.filter_window),
      rh_avg_(settings.filter_window), co2_avg_(settings.filter_window) {}

SensorError Station::begin() noexcept { return start_scd41(); }

SensorError Station::reconfigure(const StationSettings& settings) noexcept {
    settings_ = settings;
    temp_avg_ = MovingAverage<limits::kMaxFilterWindow>(settings.filter_window);
    rh_avg_ = MovingAverage<limits::kMaxFilterWindow>(settings.filter_window);
    co2_avg_ = MovingAverage<limits::kMaxFilterWindow>(settings.filter_window);
    have_scd_ = false;
    return start_scd41();
}

SensorError Station::start_scd41() noexcept {
    // The sensor may still be measuring after an MCU reset: stop it first.
    // stop_periodic is accepted in both idle and periodic mode.
    SensorError err = scd_.stop_periodic();
    if (err == SensorError::None) {
        err = scd_.reinit();
    }
    if (err == SensorError::None) {
        err = scd_.set_altitude(settings_.altitude_m);
    }
    if (err == SensorError::None) {
        err = scd_.start_periodic();
    }
    scd_running_ = err == SensorError::None;
    return err;
}

void Station::record_ok(SensorHealth& h) noexcept {
    h.consecutive = 0;
    h.last_error = SensorError::None;
}

void Station::record_error(SensorHealth& h, SensorError e) noexcept {
    ++h.errors;
    ++h.consecutive;
    h.last_error = e;
}

void Station::sample_thermo() noexcept {
    const auto m = sht_.measure();
    SensorError err = m.error;
    if (m.ok() && (m.value.temperature_c < kMinTempC || m.value.temperature_c > kMaxTempC)) {
        err = SensorError::OutOfRange;
    }
    if (err == SensorError::None) {
        temp_avg_.push(m.value.temperature_c);
        rh_avg_.push(m.value.humidity_pct);
        record_ok(sht_health_);
        return;
    }
    record_error(sht_health_, err);
    if (sht_health_.consecutive % kRecoveryThreshold == 0) {
        ++sht_health_.recoveries;
        // A failed reset is caught by the next measurement attempt.
        static_cast<void>(sht_.soft_reset());
        temp_avg_.reset();
        rh_avg_.reset();
    }
}

void Station::sample_co2(std::uint64_t timestamp_ns) noexcept {
    SensorError err = SensorError::None;
    if (scd_running_) {
        const auto ready = scd_.data_ready();
        err = ready.error;
        if (ready.ok() && ready.value) {
            const auto m = scd_.read_measurement();
            err = m.error;
            // The first reading after start-up can be 0 ppm; treat as not ready.
            if (m.ok() && m.value.co2_ppm != 0) {
                co2_avg_.push(static_cast<float>(m.value.co2_ppm));
                last_scd_ = m.value;
                have_scd_ = true;
                last_co2_ns_ = timestamp_ns;
            }
        }
    } else {
        err = SensorError::Bus;
    }
    if (err == SensorError::None) {
        record_ok(scd_health_);
        return;
    }
    record_error(scd_health_, err);
    if (scd_health_.consecutive % kRecoveryThreshold == 0) {
        ++scd_health_.recoveries;
        co2_avg_.reset();
        have_scd_ = false;
        static_cast<void>(start_scd41());
    }
}

Reading Station::sample(std::uint64_t timestamp_ns) noexcept {
    sample_thermo();
    sample_co2(timestamp_ns);

    Reading r{};
    r.seq = seq_++;
    r.timestamp_ns = timestamp_ns;
    r.device_id = settings_.device_id;

    const bool co2_fresh = have_scd_ && timestamp_ns - last_co2_ns_ <= kCo2StaleNs;
    if (!temp_avg_.empty() && sht_health_.consecutive < kRecoveryThreshold) {
        r.temperature_c = temp_avg_.mean();
        r.humidity_pct = rh_avg_.mean();
        r.flags |= flags::kThermoValid;
    } else if (co2_fresh) {
        r.temperature_c = last_scd_.temperature_c;
        r.humidity_pct = last_scd_.humidity_pct;
        r.flags |= flags::kThermoValid | flags::kThermoFallback;
    } else {
        r.temperature_c = NAN;
        r.humidity_pct = NAN;
    }
    r.dew_point_c = r.has(flags::kThermoValid) ? dew_point_c(r.temperature_c, r.humidity_pct) : NAN;

    if (co2_fresh && !co2_avg_.empty()) {
        r.co2_ppm = static_cast<std::uint16_t>(std::lround(co2_avg_.mean()));
        r.flags |= flags::kCo2Valid;
    }

    const AirQuality q = evaluate(r.has(flags::kThermoValid), r.temperature_c, r.humidity_pct,
                                  r.has(flags::kCo2Valid), r.co2_ppm, settings_.comfort);
    r.aqi = q.index;
    r.level = q.level;
    if (q.comfortable) {
        r.flags |= flags::kComfortable;
    }
    const std::uint64_t errors = static_cast<std::uint64_t>(sht_health_.errors) + scd_health_.errors;
    r.error_count = errors > UINT16_MAX ? UINT16_MAX : static_cast<std::uint16_t>(errors);
    return r;
}

}  // namespace aq
