#include <array>

#include "aq/scd41.hpp"
#include "aq/sht31.hpp"
#include "aq/sim/rng.hpp"
#include "aq/sim/room.hpp"
#include "aq/sim/sim_bus.hpp"
#include "test_framework.hpp"

TEST(rng_is_deterministic_and_bounded) {
    aq::sim::Rng a(99);
    aq::sim::Rng b(99);
    double sum = 0.0;
    double sq = 0.0;
    constexpr int kN = 20000;
    for (int i = 0; i < kN; ++i) {
        CHECK_EQ(a.next_u64(), b.next_u64());
        const double u = a.uniform();
        CHECK(u >= 0.0 && u < 1.0);
        const double n = b.normal();
        sum += n;
        sq += n * n;
        static_cast<void>(a.normal());
        static_cast<void>(b.uniform());
    }
    CHECK_NEAR(sum / kN, 0.0, 0.05);
    CHECK_NEAR(sq / kN, 1.0, 0.05);
}

TEST(room_stays_physical_over_a_week) {
    aq::sim::RoomModel room(5);
    bool window_seen = false;
    for (int i = 0; i < 7 * 24 * 60; ++i) {
        room.step(60.0);
        const auto t = room.truth();
        CHECK(t.co2_ppm >= aq::sim::RoomModel::kOutdoorCo2Ppm && t.co2_ppm < 6000.0);
        CHECK(t.temperature_c > 12.0 && t.temperature_c < 30.0);
        CHECK(t.humidity_pct >= 15.0 && t.humidity_pct <= 90.0);
        CHECK(room.occupants() >= 0 && room.occupants() <= 3);
        window_seen = window_seen || room.window_open();
    }
    CHECK(window_seen);
    CHECK_NEAR(room.elapsed_s(), 7.0 * 86400.0, 1.0);
}

TEST(empty_room_decays_to_outdoor_co2) {
    aq::sim::RoomModel room(11);
    double min_co2 = 1e9;
    for (int i = 0; i < 3 * 24 * 60; ++i) {
        room.step(60.0);
        if (room.occupants() == 0) {
            min_co2 = std::min(min_co2, room.truth().co2_ppm);
        }
    }
    CHECK(min_co2 < 900.0);
}

TEST(bus_routes_by_address_and_limits_devices) {
    aq::sim::RoomModel room(1);
    aq::sim::SimI2cBus bus(1);
    aq::sim::SimSht31 a(room);
    aq::sim::SimSht31 b(room);
    aq::sim::SimSht31 c(room);
    aq::sim::SimSht31 d(room);
    aq::sim::SimSht31 e(room);
    CHECK(bus.attach(a) && bus.attach(b) && bus.attach(c) && bus.attach(d));
    CHECK(!bus.attach(e));
    std::array<std::uint8_t, 2> cmd{0x24, 0x00};
    CHECK(bus.write(0x10, cmd.data(), cmd.size()) == aq::I2cStatus::Nack);
    CHECK(bus.read(0x10, cmd.data(), cmd.size()) == aq::I2cStatus::Nack);
    CHECK_EQ(bus.transactions(), 2U);
}

TEST(sht31_emulation_timing) {
    aq::sim::RoomModel room(1);
    aq::sim::SimI2cBus bus(1);
    aq::sim::SimSht31 sht(room);
    CHECK(bus.attach(sht));
    const std::array<std::uint8_t, 2> measure{0x24, 0x00};
    std::array<std::uint8_t, 6> frame{};
    CHECK(bus.write(0x44, measure.data(), measure.size()) == aq::I2cStatus::Ok);
    CHECK(bus.read(0x44, frame.data(), frame.size()) == aq::I2cStatus::Nack);  // still converting
    CHECK(bus.write(0x44, measure.data(), measure.size()) == aq::I2cStatus::Ok);
    bus.delay_ms(16);
    CHECK(bus.read(0x44, frame.data(), frame.size()) == aq::I2cStatus::Ok);
    CHECK(aq::sht31::parse(frame).ok());
    CHECK(bus.read(0x44, frame.data(), frame.size()) == aq::I2cStatus::Nack);  // consumed
    const std::array<std::uint8_t, 2> unknown{0x12, 0x34};
    CHECK(bus.write(0x44, unknown.data(), unknown.size()) == aq::I2cStatus::Nack);
    CHECK(bus.write(0x44, unknown.data(), 1) == aq::I2cStatus::Nack);
}

TEST(scd41_emulation_state_machine) {
    aq::sim::RoomModel room(1);
    aq::sim::SimI2cBus bus(1);
    aq::sim::SimScd41 sim(room);
    CHECK(bus.attach(sim));
    aq::Scd41 scd(bus);
    CHECK(scd.read_measurement().error == aq::SensorError::Bus);  // idle, no data
    CHECK(scd.start_periodic() == aq::SensorError::None);
    CHECK(scd.start_periodic() == aq::SensorError::Bus);  // already running
    CHECK(scd.set_altitude(100) == aq::SensorError::Bus);  // not allowed while measuring
    CHECK(scd.reinit() == aq::SensorError::Bus);
    const auto not_ready = scd.data_ready();
    CHECK(not_ready.ok() && !not_ready.value);
    bus.advance(5000);
    const auto ready = scd.data_ready();
    CHECK(ready.ok() && ready.value);
    const auto m = scd.read_measurement();
    CHECK(m.ok());
    CHECK_NEAR(m.value.co2_ppm, room.truth().co2_ppm, 50.0);
    CHECK(scd.read_measurement().error == aq::SensorError::Bus);  // consumed
    CHECK(scd.stop_periodic() == aq::SensorError::None);
    CHECK(!sim.periodic());
    CHECK(scd.set_altitude(321) == aq::SensorError::None);
    CHECK_EQ(sim.altitude_m(), 321);
    std::array<std::uint8_t, 3> word{};
    CHECK(bus.read(0x62, word.data(), word.size()) == aq::I2cStatus::Nack);  // no pending command
    const std::array<std::uint8_t, 5> bad_arg{0x24, 0x27, 0x00, 0x10, 0x00};
    CHECK(bus.write(0x62, bad_arg.data(), bad_arg.size()) == aq::I2cStatus::Nack);
    const std::array<std::uint8_t, 2> unknown{0xAB, 0xCD};
    CHECK(bus.write(0x62, unknown.data(), unknown.size()) == aq::I2cStatus::Nack);
    CHECK(bus.write(0x62, unknown.data(), 4) == aq::I2cStatus::Nack);
}

TEST(fault_injection_counts) {
    aq::sim::RoomModel room(1);
    aq::sim::SimI2cBus bus(1);
    aq::sim::SimSht31 sht(room);
    CHECK(bus.attach(sht));
    bus.set_fault_rate(1.0);
    aq::Sht31 drv(bus);
    CHECK(drv.measure().error == aq::SensorError::Bus);
    CHECK_EQ(bus.faults_injected(), 1U);
}

TEST_MAIN()
