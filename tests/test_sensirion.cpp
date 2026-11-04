#include "aq/sensirion.hpp"

#include <array>
#include <string_view>

#include "test_framework.hpp"

TEST(word_round_trip) {
    std::array<std::uint8_t, 3> w{};
    for (std::uint32_t v = 0; v <= 0xFFFF; v += 257) {
        aq::sensirion::encode_word(static_cast<std::uint16_t>(v), w.data());
        const auto back = aq::sensirion::decode_word(w.data());
        CHECK(back.has_value());
        CHECK_EQ(*back, v);
    }
}

TEST(word_rejects_bad_crc) {
    std::array<std::uint8_t, 3> w{0xBE, 0xEF, 0x92};
    CHECK(aq::sensirion::decode_word(w.data()).has_value());
    w[2] = 0x93;
    CHECK(!aq::sensirion::decode_word(w.data()).has_value());
    w = {0xBF, 0xEF, 0x92};
    CHECK(!aq::sensirion::decode_word(w.data()).has_value());
}

TEST(conversion_endpoints) {
    CHECK_NEAR(aq::sensirion::ticks_to_celsius(0), -45.0, 1e-4);
    CHECK_NEAR(aq::sensirion::ticks_to_celsius(65535), 130.0, 1e-4);
    CHECK_NEAR(aq::sensirion::ticks_to_rh(0), 0.0, 1e-4);
    CHECK_NEAR(aq::sensirion::ticks_to_rh(65535), 100.0, 1e-4);
    // Datasheet example: 0x6666 -> 25.0 C (approx), 0x8000 -> 50 %RH.
    CHECK_NEAR(aq::sensirion::ticks_to_celsius(0x6666), 25.0, 0.01);
    CHECK_NEAR(aq::sensirion::ticks_to_rh(0x8000), 50.0, 0.01);
}

TEST(inverse_conversions_clamp_and_round_trip) {
    for (float t = -40.0F; t <= 120.0F; t += 7.3F) {
        CHECK_NEAR(aq::sensirion::ticks_to_celsius(aq::sensirion::celsius_to_ticks(t)), t, 0.002);
    }
    CHECK_EQ(aq::sensirion::celsius_to_ticks(-100.0F), 0);
    CHECK_EQ(aq::sensirion::celsius_to_ticks(500.0F), 65535);
    CHECK_EQ(aq::sensirion::rh_to_ticks(-5.0F), 0);
    CHECK_EQ(aq::sensirion::rh_to_ticks(150.0F), 65535);
}

TEST(error_names) {
    CHECK(std::string_view(aq::to_string(aq::SensorError::Crc)) == "CRC mismatch");
    CHECK(std::string_view(aq::to_string(aq::SensorError::None)) == "ok");
    CHECK(std::string_view(aq::to_string(aq::SensorError::NotReady)) == "not ready");
    CHECK(std::string_view(aq::to_string(aq::SensorError::OutOfRange)) == "out of range");
    CHECK(std::string_view(aq::to_string(aq::SensorError::Bus)) == "bus error");
}

TEST_MAIN()
