#include <array>

#include "aq/scd41.hpp"
#include "aq/sensirion.hpp"
#include "aq/sht31.hpp"
#include "fake_bus.hpp"
#include "test_framework.hpp"

namespace {

std::array<std::uint8_t, 6> sht_frame(std::uint16_t t, std::uint16_t rh) {
    std::array<std::uint8_t, 6> f{};
    aq::sensirion::encode_word(t, &f[0]);
    aq::sensirion::encode_word(rh, &f[3]);
    return f;
}

std::array<std::uint8_t, 9> scd_frame(std::uint16_t co2, std::uint16_t t, std::uint16_t rh) {
    std::array<std::uint8_t, 9> f{};
    aq::sensirion::encode_word(co2, &f[0]);
    aq::sensirion::encode_word(t, &f[3]);
    aq::sensirion::encode_word(rh, &f[6]);
    return f;
}

}  // namespace

TEST(sht31_measure_sends_command_and_converts) {
    aqtest::ScriptedBus bus;
    const auto f = sht_frame(0x6666, 0x8000);
    bus.queue_read(f.data(), f.size());
    aq::Sht31 sht(bus);
    const auto r = sht.measure();
    CHECK(r.ok());
    CHECK_EQ(bus.last_address, 0x44);
    CHECK_EQ(bus.last_command(), 0x2400);
    CHECK(bus.delayed_ms >= 15);
    CHECK_NEAR(r.value.temperature_c, 25.0, 0.01);
    CHECK_NEAR(r.value.humidity_pct, 50.0, 0.01);
}

TEST(sht31_crc_error_on_either_word) {
    auto f = sht_frame(0x6666, 0x8000);
    f[2] ^= 0x01;
    CHECK(aq::sht31::parse(f).error == aq::SensorError::Crc);
    f = sht_frame(0x6666, 0x8000);
    f[4] ^= 0x80;
    CHECK(aq::sht31::parse(f).error == aq::SensorError::Crc);
}

TEST(sht31_bus_errors_propagate) {
    aqtest::ScriptedBus bus;
    aq::Sht31 sht(bus, 0x45);
    bus.next_write_status = aq::I2cStatus::Nack;
    CHECK(sht.measure().error == aq::SensorError::Bus);
    // Write succeeds, read times out.
    const std::array<std::uint8_t, 1> dummy{0};
    bus.queue_read(dummy.data(), 0, aq::I2cStatus::Timeout);
    CHECK(sht.measure().error == aq::SensorError::Bus);
    CHECK_EQ(sht.address(), 0x45);
}

TEST(sht31_soft_reset_command) {
    aqtest::ScriptedBus bus;
    aq::Sht31 sht(bus);
    CHECK(sht.soft_reset() == aq::SensorError::None);
    CHECK_EQ(bus.last_command(), 0x30A2);
}

TEST(scd41_read_measurement) {
    aqtest::ScriptedBus bus;
    const auto f = scd_frame(812, 0x6666, 0x8000);
    bus.queue_read(f.data(), f.size());
    aq::Scd41 scd(bus);
    const auto r = scd.read_measurement();
    CHECK(r.ok());
    CHECK_EQ(bus.last_address, 0x62);
    CHECK_EQ(bus.last_command(), 0xEC05);
    CHECK_EQ(r.value.co2_ppm, 812);
    CHECK_NEAR(r.value.temperature_c, 25.0, 0.01);
}

TEST(scd41_parse_rejects_crc_and_range) {
    auto f = scd_frame(812, 0x6666, 0x8000);
    f[8] ^= 0x10;
    CHECK(aq::scd41::parse(f).error == aq::SensorError::Crc);
    f = scd_frame(50000, 0x6666, 0x8000);
    CHECK(aq::scd41::parse(f).error == aq::SensorError::OutOfRange);
}

TEST(scd41_data_ready_status_bits) {
    CHECK(!aq::scd41::is_data_ready(0x8000));
    CHECK(aq::scd41::is_data_ready(0x8006));
    CHECK(aq::scd41::is_data_ready(0x0001));
    CHECK(!aq::scd41::is_data_ready(0xF800));

    aqtest::ScriptedBus bus;
    std::array<std::uint8_t, 3> w{};
    aq::sensirion::encode_word(0x8006, w.data());
    bus.queue_read(w.data(), w.size());
    aq::Scd41 scd(bus);
    const auto r = scd.data_ready();
    CHECK(r.ok() && r.value);
    CHECK_EQ(bus.last_command(), 0xE4B8);

    w[2] ^= 0xFF;
    bus.queue_read(w.data(), w.size());
    CHECK(scd.data_ready().error == aq::SensorError::Crc);
}

TEST(scd41_set_altitude_frames_argument) {
    aqtest::ScriptedBus bus;
    aq::Scd41 scd(bus);
    CHECK(scd.set_altitude(850) == aq::SensorError::None);
    CHECK_EQ(bus.last_write.len, 5U);
    CHECK_EQ(bus.last_command(), 0x2427);
    const auto arg = aq::sensirion::decode_word(&bus.last_write.bytes[2]);
    CHECK(arg.has_value() && *arg == 850);
}

TEST(scd41_control_commands_and_delays) {
    aqtest::ScriptedBus bus;
    aq::Scd41 scd(bus);
    CHECK(scd.start_periodic() == aq::SensorError::None);
    CHECK_EQ(bus.last_command(), 0x21B1);
    CHECK(scd.stop_periodic() == aq::SensorError::None);
    CHECK_EQ(bus.last_command(), 0x3F86);
    CHECK(bus.delayed_ms >= 500);
    CHECK(scd.reinit() == aq::SensorError::None);
    CHECK_EQ(bus.last_command(), 0x3646);
    bus.next_write_status = aq::I2cStatus::BusError;
    CHECK(scd.start_periodic() == aq::SensorError::Bus);
    bus.next_write_status = aq::I2cStatus::Nack;
    CHECK(scd.data_ready().error == aq::SensorError::Bus);
    bus.next_write_status = aq::I2cStatus::Nack;
    CHECK(scd.read_measurement().error == aq::SensorError::Bus);
    CHECK(scd.read_measurement().error == aq::SensorError::Bus);  // nothing queued: read NACK
    const std::array<std::uint8_t, 1> dummy{0};
    bus.queue_read(dummy.data(), 0, aq::I2cStatus::Nack);
    CHECK(scd.data_ready().error == aq::SensorError::Bus);
    CHECK_EQ(scd.address(), 0x62);
}

TEST_MAIN()
