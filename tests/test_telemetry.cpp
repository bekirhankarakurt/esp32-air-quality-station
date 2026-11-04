#include "aq/telemetry.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

#include "aq/crc.hpp"
#include "test_framework.hpp"

namespace {

aq::Reading sample() {
    aq::Reading r{};
    r.seq = 0x0102030405060708ULL;
    r.timestamp_ns = 123'456'789'000ULL;
    r.device_id = 0xA1B2C3D4U;
    r.temperature_c = 22.5F;
    r.humidity_pct = 41.25F;
    r.dew_point_c = 8.75F;
    r.co2_ppm = 845;
    r.aqi = 61;
    r.level = aq::AirLevel::Moderate;
    r.flags = aq::flags::kThermoValid | aq::flags::kCo2Valid | aq::flags::kComfortable;
    r.error_count = 3;
    return r;
}

std::string json_of(const aq::Reading& r) {
    std::array<char, aq::kJsonBufferSize> buf{};
    const std::size_t n = aq::encode_json(r, buf.data(), buf.size());
    return std::string(buf.data(), n);
}

}  // namespace

TEST(binary_round_trip) {
    const auto r = sample();
    const auto pkt = aq::encode_binary(r);
    const auto d = aq::decode_binary(pkt.data(), pkt.size());
    CHECK(d.ok());
    CHECK(d.reading == r);
}

TEST(binary_header_layout_matches_daemon_framing) {
    const auto pkt = aq::encode_binary(sample());
    // "TLMY", version 1, type 2, payload length 24, big-endian sequence.
    const std::array<std::uint8_t, 16> expected{0x54, 0x4C, 0x4D, 0x59, 0x01, 0x02, 0x00, 0x18,
                                                0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    CHECK(std::memcmp(pkt.data(), expected.data(), expected.size()) == 0);
    // CO2 845 = 0x034D at payload offset 16.
    CHECK_EQ(pkt[24 + 16], 0x03);
    CHECK_EQ(pkt[24 + 17], 0x4D);
    // 22.5f = 0x41B40000 at payload offset 4.
    CHECK_EQ(pkt[28], 0x41);
    CHECK_EQ(pkt[29], 0xB4);
    const std::uint32_t crc = aq::crc32(pkt.data(), 48);
    CHECK_EQ(pkt[48], static_cast<std::uint8_t>(crc >> 24));
    CHECK_EQ(pkt[51], static_cast<std::uint8_t>(crc));
}

TEST(binary_rejections) {
    const auto good = aq::encode_binary(sample());
    CHECK(aq::decode_binary(nullptr, 0).error == aq::DecodeError::TooShort);
    CHECK(aq::decode_binary(good.data(), 10).error == aq::DecodeError::TooShort);
    CHECK(aq::decode_binary(good.data(), good.size() - 1).error == aq::DecodeError::BadLength);

    auto p = good;
    p[0] = 0x00;
    CHECK(aq::decode_binary(p.data(), p.size()).error == aq::DecodeError::BadMagic);
    p = good;
    p[4] = 2;
    CHECK(aq::decode_binary(p.data(), p.size()).error == aq::DecodeError::BadVersion);
    p = good;
    p[5] = 1;  // embedded-telemetry-daemon system packet
    CHECK(aq::decode_binary(p.data(), p.size()).error == aq::DecodeError::BadType);
    p = good;
    p[7] = 25;
    CHECK(aq::decode_binary(p.data(), p.size()).error == aq::DecodeError::BadLength);
    p = good;
    p[30] ^= 0x04;
    CHECK(aq::decode_binary(p.data(), p.size()).error == aq::DecodeError::BadCrc);

    // Valid CRC but an impossible level value.
    p = good;
    p[44] = 9;
    const std::uint32_t crc = aq::crc32(p.data(), 48);
    for (int i = 0; i < 4; ++i) {
        p[48 + i] = static_cast<std::uint8_t>(crc >> (24 - 8 * i));
    }
    CHECK(aq::decode_binary(p.data(), p.size()).error == aq::DecodeError::BadValue);
    CHECK(std::string_view(aq::to_string(aq::DecodeError::BadValue)) == "bad value");
    CHECK(std::string_view(aq::to_string(aq::DecodeError::BadCrc)) == "CRC mismatch");
}

TEST(binary_every_bit_flip_detected) {
    const auto good = aq::encode_binary(sample());
    for (std::size_t bit = 0; bit < good.size() * 8; ++bit) {
        auto p = good;
        p[bit / 8] ^= static_cast<std::uint8_t>(1U << (bit % 8));
        CHECK(!aq::decode_binary(p.data(), p.size()).ok());
    }
}

TEST(json_exact_output) {
    CHECK(json_of(sample()) ==
          "{\"type\":\"air\",\"seq\":72623859790382856,\"ts_ns\":123456789000,\"device\":\"A1B2C3D4\","
          "\"temp_c\":22.50,\"rh_pct\":41.25,\"dew_c\":8.75,\"co2_ppm\":845,\"aqi\":61,\"level\":\"moderate\","
          "\"flags\":11,\"errors\":3}\n");
}

TEST(json_round_trip) {
    const auto r = sample();
    const auto parsed = aq::parse_json(json_of(r));
    CHECK(parsed.has_value());
    CHECK(*parsed == r);
}

TEST(json_invalid_fields_are_null) {
    auto r = sample();
    r.flags = 0;
    r.temperature_c = NAN;
    const std::string s = json_of(r);
    CHECK(s.find("\"temp_c\":null,\"rh_pct\":null,\"dew_c\":null,\"co2_ppm\":null") != std::string::npos);
    const auto parsed = aq::parse_json(s);
    CHECK(parsed.has_value());
    CHECK(std::isnan(parsed->temperature_c));
    CHECK(std::isnan(parsed->dew_point_c));
    CHECK_EQ(parsed->co2_ppm, 0);
}

TEST(json_buffer_too_small) {
    std::array<char, 16> small{};
    CHECK_EQ(aq::encode_json(sample(), small.data(), small.size()), 0U);
    CHECK_EQ(small[0], '\0');
    CHECK_EQ(aq::encode_json(sample(), nullptr, 0), 0U);
}

TEST(json_rejects_malformed) {
    const std::string good = json_of(sample());
    CHECK(aq::parse_json(good).has_value());
    CHECK(!aq::parse_json("").has_value());
    CHECK(!aq::parse_json("{}").has_value());
    CHECK(!aq::parse_json(good.substr(0, good.size() - 3)).has_value());
    CHECK(!aq::parse_json(good + "x").has_value());

    auto replace = [&](std::string_view from, std::string_view to) {
        std::string s = good;
        s.replace(s.find(from), from.size(), to);
        return aq::parse_json(s).has_value();
    };
    CHECK(!replace("\"air\"", "\"sys\""));
    CHECK(!replace("\"moderate\"", "\"meh\""));
    CHECK(!replace("A1B2C3D4", "XYZ"));
    CHECK(!replace("\"co2_ppm\":845", "\"co2_ppm\":70000"));
    CHECK(!replace("\"co2_ppm\":845", "\"co2_ppm\":-5"));
    CHECK(!replace("\"flags\":11", "\"flags\":300"));
    CHECK(!replace("\"aqi\":61", "\"aqi\":null"));
    CHECK(!replace("\"temp_c\":22.50", "\"temp_c\":abc"));
    CHECK(!replace("\"errors\":3", "\"bogus\":3"));
    CHECK(!replace("\"seq\":", "\"seq\" "));
    CHECK(!replace(",\"aqi\"", " \"aqi\""));
    CHECK(!replace("\"temp_c\":22.50", "\"temp_c\":null"));  // contradicts flags
    CHECK(!replace("\"device\":\"A1B2C3D4\"", "\"device\":\"A1\\\"\""));
    CHECK(replace("{\"type\"", "{ \"type\""));
}

TEST(json_detection) {
    const std::string j = " \n{\"a\":1}";
    CHECK(aq::looks_like_json(reinterpret_cast<const std::uint8_t*>(j.data()), j.size()));
    const auto pkt = aq::encode_binary(sample());
    CHECK(!aq::looks_like_json(pkt.data(), pkt.size()));
    CHECK(!aq::looks_like_json(pkt.data(), 0));
}

TEST_MAIN()
