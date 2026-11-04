#include "aq/station.hpp"

#include <cmath>

#include "aq/sim/room.hpp"
#include "aq/sim/sim_bus.hpp"
#include "test_framework.hpp"

namespace {

constexpr std::uint64_t kPeriodMs = 5000;

struct Rig {
    aq::sim::RoomModel room{42};
    aq::sim::SimI2cBus bus{7};
    aq::sim::SimSht31 sht{room};
    aq::sim::SimScd41 scd{room};
    aq::Station station;

    explicit Rig(bool with_scd = true, std::uint16_t altitude = 0)
        : station(bus, aq::StationSettings{0xCAFEF00DU, 4, altitude, {}}) {
        CHECK(bus.attach(sht));
        if (with_scd) {
            CHECK(bus.attach(scd));
        }
    }

    aq::Reading tick() {
        room.step(static_cast<double>(kPeriodMs) / 1000.0);
        bus.advance(kPeriodMs);
        return station.sample(bus.now_ms() * 1'000'000ULL);
    }
};

}  // namespace

TEST(begin_configures_scd41) {
    Rig rig(true, 850);
    CHECK(rig.station.begin() == aq::SensorError::None);
    CHECK(rig.scd.periodic());
    CHECK_EQ(rig.scd.altitude_m(), 850);
}

TEST(nominal_readings_track_truth) {
    Rig rig;
    CHECK(rig.station.begin() == aq::SensorError::None);
    aq::Reading r{};
    for (int i = 0; i < 20; ++i) {
        r = rig.tick();
        CHECK_EQ(r.seq, static_cast<std::uint64_t>(i));
    }
    const auto truth = rig.room.truth();
    CHECK(r.has(aq::flags::kThermoValid));
    CHECK(r.has(aq::flags::kCo2Valid));
    CHECK(!r.has(aq::flags::kThermoFallback));
    CHECK_NEAR(r.temperature_c, truth.temperature_c, 0.3);
    CHECK_NEAR(r.humidity_pct, truth.humidity_pct, 1.5);
    CHECK_NEAR(r.co2_ppm, truth.co2_ppm, 40.0);
    CHECK_EQ(r.device_id, 0xCAFEF00DU);
    CHECK_EQ(r.error_count, 0);
    CHECK_EQ(r.aqi, aq::evaluate(true, r.temperature_c, r.humidity_pct, true, r.co2_ppm).index);
    CHECK_NEAR(r.dew_point_c, aq::dew_point_c(r.temperature_c, r.humidity_pct), 1e-4);
}

TEST(co2_invalid_until_first_measurement) {
    Rig rig;
    CHECK(rig.station.begin() == aq::SensorError::None);
    // First sample happens exactly at the SCD41's first 5 s boundary minus the
    // start-up delays, so the first CO2 value appears within two periods.
    const auto first = rig.station.sample(0);
    CHECK(!first.has(aq::flags::kCo2Valid));
    CHECK(first.has(aq::flags::kThermoValid));
    const auto later = rig.tick();
    CHECK(later.has(aq::flags::kCo2Valid));
}

TEST(missing_scd41_triggers_recovery_and_stays_invalid) {
    Rig rig(false);
    CHECK(rig.station.begin() == aq::SensorError::Bus);
    aq::Reading r{};
    for (int i = 0; i < 9; ++i) {
        r = rig.tick();
    }
    CHECK(!r.has(aq::flags::kCo2Valid));
    CHECK(r.has(aq::flags::kThermoValid));
    CHECK_EQ(rig.station.scd41_health().recoveries, 3U);
    CHECK(rig.station.scd41_health().errors >= 9U);
    CHECK(r.error_count >= 9);
}

TEST(sht31_failure_falls_back_to_scd41) {
    aq::sim::RoomModel room{1};
    aq::sim::SimI2cBus bus{2};
    aq::sim::SimScd41 scd{room};
    CHECK(bus.attach(scd));
    aq::Station station(bus, aq::StationSettings{});
    CHECK(station.begin() == aq::SensorError::None);
    aq::Reading r{};
    for (int i = 0; i < 6; ++i) {
        room.step(5.0);
        bus.advance(kPeriodMs);
        r = station.sample(bus.now_ms() * 1'000'000ULL);
    }
    CHECK(r.has(aq::flags::kThermoValid));
    CHECK(r.has(aq::flags::kThermoFallback));
    // SCD41 reads warm because of self-heating.
    CHECK(r.temperature_c > static_cast<float>(room.truth().temperature_c));
    CHECK_EQ(station.sht31_health().recoveries, 2U);
}

TEST(no_sensors_at_all) {
    aq::sim::SimI2cBus bus{3};
    aq::Station station(bus, aq::StationSettings{});
    CHECK(station.begin() == aq::SensorError::Bus);
    const auto r = station.sample(1);
    CHECK_EQ(r.flags, 0);
    CHECK(std::isnan(r.temperature_c));
    CHECK(std::isnan(r.dew_point_c));
    CHECK_EQ(r.aqi, 0);
}

TEST(survives_heavy_fault_injection) {
    Rig rig;
    rig.bus.set_fault_rate(0.10);
    static_cast<void>(rig.station.begin());
    int thermo_valid = 0;
    int co2_valid = 0;
    constexpr int kSamples = 400;
    for (int i = 0; i < kSamples; ++i) {
        const auto r = rig.tick();
        thermo_valid += r.has(aq::flags::kThermoValid) ? 1 : 0;
        co2_valid += r.has(aq::flags::kCo2Valid) ? 1 : 0;
        if (r.has(aq::flags::kThermoValid) && !r.has(aq::flags::kThermoFallback)) {
            // Corrupted frames must never leak through the CRC check.
            CHECK_NEAR(r.temperature_c, rig.room.truth().temperature_c, 1.0);
        }
        if (r.has(aq::flags::kCo2Valid)) {
            CHECK_NEAR(r.co2_ppm, rig.room.truth().co2_ppm, 150.0);
        }
    }
    CHECK(rig.bus.faults_injected() > 50U);
    CHECK(thermo_valid > kSamples * 9 / 10);
    CHECK(co2_valid > kSamples * 8 / 10);
    CHECK(rig.station.sht31_health().errors > 0U);
}

TEST(stale_co2_is_invalidated) {
    Rig rig;
    CHECK(rig.station.begin() == aq::SensorError::None);
    for (int i = 0; i < 4; ++i) {
        static_cast<void>(rig.tick());
    }
    // Sample faster than the SCD41 produces data: still valid within 30 s...
    const std::uint64_t t0 = rig.bus.now_ms() * 1'000'000ULL;
    CHECK(rig.station.sample(t0 + 1'000'000ULL).has(aq::flags::kCo2Valid));
    // ...but a reading 31 s after the last CO2 update is stale.
    CHECK(!rig.station.sample(t0 + 31'000'000'000ULL).has(aq::flags::kCo2Valid));
}

TEST(reconfigure_applies_altitude_and_window) {
    Rig rig;
    CHECK(rig.station.begin() == aq::SensorError::None);
    static_cast<void>(rig.tick());
    aq::StationSettings s = rig.station.settings();
    s.altitude_m = 1200;
    s.filter_window = 2;
    CHECK(rig.station.reconfigure(s) == aq::SensorError::None);
    CHECK_EQ(rig.scd.altitude_m(), 1200);
    CHECK(rig.scd.periodic());
    CHECK_EQ(rig.station.settings().filter_window, 2);
}

TEST_MAIN()
