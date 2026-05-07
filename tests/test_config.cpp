#include "aq/config.hpp"

#include <array>
#include <string_view>

#include "test_framework.hpp"

TEST(ipv4_parse) {
    const auto ip = aq::parse_ipv4("192.168.1.42");
    CHECK(ip.has_value() && *ip == (aq::Ipv4{192, 168, 1, 42}));
    CHECK(aq::parse_ipv4("0.0.0.0").has_value());
    CHECK(aq::parse_ipv4("255.255.255.255").has_value());
    for (const char* bad : {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "01.2.3.4", "1..3.4", "a.b.c.d", " 1.2.3.4",
                            "1.2.3.4 ", "1.2.3.", "-1.2.3.4", "99999999999.1.1.1"}) {
        CHECK(!aq::parse_ipv4(bad).has_value());
    }
}

TEST(ipv4_format) {
    std::array<char, 16> buf{};
    CHECK(aq::format_ipv4({10, 0, 0, 255}, buf.data(), buf.size()));
    CHECK(std::string_view(buf.data()) == "10.0.0.255");
    std::array<char, 8> small{};
    CHECK(!aq::format_ipv4({100, 100, 100, 100}, small.data(), small.size()));
}

TEST(format_names) {
    CHECK(aq::parse_format("json") == aq::TelemetryFormat::Json);
    CHECK(aq::parse_format("both") == aq::TelemetryFormat::Both);
    CHECK(aq::parse_format("binary") == aq::TelemetryFormat::Binary);
    CHECK(!aq::parse_format("xml").has_value());
}

TEST(validate_limits) {
    aq::StationConfig c{};
    CHECK(aq::validate(c) == aq::ConfigError::None);
    auto bad = c;
    bad.target_ip = {0, 0, 0, 0};
    CHECK(aq::validate(bad) == aq::ConfigError::BadAddress);
    bad = c;
    bad.target_port = 0;
    CHECK(aq::validate(bad) == aq::ConfigError::BadPort);
    bad = c;
    bad.sample_period_ms = 999;
    CHECK(aq::validate(bad) == aq::ConfigError::BadPeriod);
    bad = c;
    bad.format = static_cast<aq::TelemetryFormat>(7);
    CHECK(aq::validate(bad) == aq::ConfigError::BadFormat);
    bad = c;
    bad.altitude_m = 3001;
    CHECK(aq::validate(bad) == aq::ConfigError::BadAltitude);
    bad = c;
    bad.filter_window = 0;
    CHECK(aq::validate(bad) == aq::ConfigError::BadWindow);
    CHECK(std::string_view(aq::to_string(aq::ConfigError::BadPort)) == "port must be 1-65535");
}

TEST(blob_round_trip_and_corruption) {
    aq::StationConfig c{};
    c.target_ip = {10, 1, 2, 3};
    c.target_port = 9000;
    c.sample_period_ms = 30000;
    c.format = aq::TelemetryFormat::Both;
    c.altitude_m = 1100;
    c.filter_window = 12;
    const auto blob = aq::serialize(c);
    const auto back = aq::deserialize(blob.data(), blob.size());
    CHECK(back.has_value() && *back == c);

    CHECK(!aq::deserialize(blob.data(), blob.size() - 1).has_value());
    CHECK(!aq::deserialize(nullptr, blob.size()).has_value());
    for (std::size_t i = 0; i < blob.size(); ++i) {
        auto b = blob;
        b[i] ^= 0x20;
        CHECK(!aq::deserialize(b.data(), b.size()).has_value());
    }
}

TEST(form_applies_fields) {
    aq::StationConfig c{};
    CHECK(aq::apply_form("ip=10.0.0.7&port=5000&period_ms=10000&format=json&altitude_m=900&window=4", c) ==
          aq::ConfigError::None);
    CHECK(c.target_ip == (aq::Ipv4{10, 0, 0, 7}));
    CHECK_EQ(c.target_port, 5000);
    CHECK_EQ(c.sample_period_ms, 10000U);
    CHECK(c.format == aq::TelemetryFormat::Json);
    CHECK_EQ(c.altitude_m, 900);
    CHECK_EQ(c.filter_window, 4);
    // Percent-encoding and empty pairs.
    CHECK(aq::apply_form("ip=192%2E168%2e0%2E9&&format=both", c) == aq::ConfigError::None);
    CHECK(c.target_ip == (aq::Ipv4{192, 168, 0, 9}));
    CHECK(c.format == aq::TelemetryFormat::Both);
    CHECK(aq::apply_form("", c) == aq::ConfigError::None);
}

TEST(form_errors_leave_config_unchanged) {
    const aq::StationConfig orig{};
    aq::StationConfig c = orig;
    CHECK(aq::apply_form("port=5000&ip=300.1.1.1", c) == aq::ConfigError::BadAddress);
    CHECK(aq::apply_form("port=70000", c) == aq::ConfigError::BadPort);
    CHECK(aq::apply_form("port=0", c) == aq::ConfigError::BadPort);
    CHECK(aq::apply_form("port", c) == aq::ConfigError::BadPort);
    CHECK(aq::apply_form("period_ms=500", c) == aq::ConfigError::BadPeriod);
    CHECK(aq::apply_form("period_ms=x", c) == aq::ConfigError::BadPeriod);
    CHECK(aq::apply_form("format=xml", c) == aq::ConfigError::BadFormat);
    CHECK(aq::apply_form("altitude_m=4000", c) == aq::ConfigError::BadAltitude);
    CHECK(aq::apply_form("window=33", c) == aq::ConfigError::BadWindow);
    CHECK(aq::apply_form("window=0", c) == aq::ConfigError::BadWindow);
    CHECK(aq::apply_form("ssid=evil", c) == aq::ConfigError::UnknownField);
    CHECK(aq::apply_form("ip=1.2.3.4%", c) == aq::ConfigError::BadAddress);
    CHECK(aq::apply_form("ip=1.2.3.4%zz", c) == aq::ConfigError::BadAddress);
    CHECK(aq::apply_form("ip=111111111111111111111111111111111111", c) == aq::ConfigError::BadAddress);
    CHECK(c == orig);
}

TEST_MAIN()
