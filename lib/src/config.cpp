#include "aq/config.hpp"

#include <cstdio>
#include <cstring>

#include "aq/crc.hpp"

namespace aq {
namespace {

constexpr std::uint8_t kBlobVersion = 1;

std::optional<std::uint32_t> parse_decimal(std::string_view s, std::uint32_t max) noexcept {
    if (s.empty() || s.size() > 10 || (s.size() > 1 && s[0] == '0')) {
        return std::nullopt;
    }
    std::uint64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        v = v * 10U + static_cast<std::uint64_t>(c - '0');
    }
    if (v > max) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(v);
}

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/// URL-decode @p in into @p out; returns the decoded view or nullopt on bad escapes / overflow.
template <std::size_t N>
std::optional<std::string_view> url_decode(std::string_view in, std::array<char, N>& out) noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (n >= N) {
            return std::nullopt;
        }
        char c = in[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 2 >= in.size()) {
                return std::nullopt;
            }
            const int hi = hex_value(in[i + 1]);
            const int lo = hex_value(in[i + 2]);
            if (hi < 0 || lo < 0) {
                return std::nullopt;
            }
            c = static_cast<char>(hi * 16 + lo);
            i += 2;
        }
        out[n++] = c;
    }
    return std::string_view(out.data(), n);
}

void put_u16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 8U);
    p[1] = static_cast<std::uint8_t>(v);
}

void put_u32(std::uint8_t* p, std::uint32_t v) noexcept {
    put_u16(p, static_cast<std::uint16_t>(v >> 16U));
    put_u16(p + 2, static_cast<std::uint16_t>(v));
}

std::uint16_t get_u16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) | p[1]);
}

std::uint32_t get_u32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(get_u16(p)) << 16U) | get_u16(p + 2);
}

}  // namespace

const char* to_string(TelemetryFormat f) noexcept {
    switch (f) {
        case TelemetryFormat::Binary: return "binary";
        case TelemetryFormat::Json: return "json";
        case TelemetryFormat::Both: return "both";
    }
    return "unknown";
}

std::optional<TelemetryFormat> parse_format(std::string_view s) noexcept {
    for (const auto f : {TelemetryFormat::Binary, TelemetryFormat::Json, TelemetryFormat::Both}) {
        if (s == to_string(f)) {
            return f;
        }
    }
    return std::nullopt;
}

std::optional<Ipv4> parse_ipv4(std::string_view s) noexcept {
    Ipv4 ip{};
    for (std::size_t octet = 0; octet < ip.size(); ++octet) {
        const std::size_t dot = s.find('.');
        const bool last = octet + 1 == ip.size();
        if (last != (dot == std::string_view::npos)) {
            return std::nullopt;
        }
        const auto v = parse_decimal(s.substr(0, dot), 255);
        if (!v) {
            return std::nullopt;
        }
        ip[octet] = static_cast<std::uint8_t>(*v);
        s = last ? std::string_view{} : s.substr(dot + 1);
    }
    return ip;
}

bool format_ipv4(const Ipv4& ip, char* out, std::size_t capacity) noexcept {
    const int n = std::snprintf(out, capacity, "%u.%u.%u.%u", static_cast<unsigned>(ip[0]),
                                static_cast<unsigned>(ip[1]), static_cast<unsigned>(ip[2]),
                                static_cast<unsigned>(ip[3]));
    return n > 0 && static_cast<std::size_t>(n) < capacity;
}

const char* to_string(ConfigError e) noexcept {
    switch (e) {
        case ConfigError::None: return "ok";
        case ConfigError::BadAddress: return "invalid IPv4 address";
        case ConfigError::BadPort: return "port must be 1-65535";
        case ConfigError::BadPeriod: return "period must be 1000-3600000 ms";
        case ConfigError::BadFormat: return "format must be binary, json or both";
        case ConfigError::BadAltitude: return "altitude must be 0-3000 m";
        case ConfigError::BadWindow: return "filter window must be 1-32";
        case ConfigError::UnknownField: return "unknown field";
    }
    return "unknown";
}

ConfigError validate(const StationConfig& c) noexcept {
    if (c.target_ip == Ipv4{0, 0, 0, 0}) {
        return ConfigError::BadAddress;
    }
    if (c.target_port == 0) {
        return ConfigError::BadPort;
    }
    if (c.sample_period_ms < limits::kMinPeriodMs || c.sample_period_ms > limits::kMaxPeriodMs) {
        return ConfigError::BadPeriod;
    }
    if (static_cast<std::uint8_t>(c.format) > static_cast<std::uint8_t>(TelemetryFormat::Both)) {
        return ConfigError::BadFormat;
    }
    if (c.altitude_m > limits::kMaxAltitudeM) {
        return ConfigError::BadAltitude;
    }
    if (c.filter_window == 0 || c.filter_window > limits::kMaxFilterWindow) {
        return ConfigError::BadWindow;
    }
    return ConfigError::None;
}

ConfigBlob serialize(const StationConfig& c) noexcept {
    ConfigBlob b{};
    b[0] = kBlobVersion;
    std::memcpy(&b[1], c.target_ip.data(), c.target_ip.size());
    put_u16(&b[5], c.target_port);
    put_u32(&b[7], c.sample_period_ms);
    b[11] = static_cast<std::uint8_t>(c.format);
    put_u16(&b[12], c.altitude_m);
    b[14] = c.filter_window;
    b[15] = 0;
    put_u32(&b[16], crc32(b.data(), 16));
    return b;
}

std::optional<StationConfig> deserialize(const std::uint8_t* data, std::size_t len) noexcept {
    if (data == nullptr || len != kConfigBlobSize || data[0] != kBlobVersion ||
        get_u32(&data[16]) != crc32(data, 16)) {
        return std::nullopt;
    }
    StationConfig c{};
    std::memcpy(c.target_ip.data(), &data[1], c.target_ip.size());
    c.target_port = get_u16(&data[5]);
    c.sample_period_ms = get_u32(&data[7]);
    c.format = static_cast<TelemetryFormat>(data[11]);
    c.altitude_m = get_u16(&data[12]);
    c.filter_window = data[14];
    if (validate(c) != ConfigError::None) {
        return std::nullopt;
    }
    return c;
}

ConfigError apply_form(std::string_view body, StationConfig& base) noexcept {
    StationConfig c = base;
    while (!body.empty()) {
        const std::size_t amp = body.find('&');
        const std::string_view pair = body.substr(0, amp);
        body = amp == std::string_view::npos ? std::string_view{} : body.substr(amp + 1);
        if (pair.empty()) {
            continue;
        }
        const std::size_t eq = pair.find('=');
        const std::string_view key = pair.substr(0, eq);
        std::array<char, 32> buf{};
        const auto value =
            url_decode(eq == std::string_view::npos ? std::string_view{} : pair.substr(eq + 1), buf);
        if (key == "ip") {
            const auto ip = value ? parse_ipv4(*value) : std::nullopt;
            if (!ip) {
                return ConfigError::BadAddress;
            }
            c.target_ip = *ip;
        } else if (key == "port") {
            const auto v = value ? parse_decimal(*value, 65535) : std::nullopt;
            if (!v) {
                return ConfigError::BadPort;
            }
            c.target_port = static_cast<std::uint16_t>(*v);
        } else if (key == "period_ms") {
            const auto v = value ? parse_decimal(*value, limits::kMaxPeriodMs) : std::nullopt;
            if (!v) {
                return ConfigError::BadPeriod;
            }
            c.sample_period_ms = *v;
        } else if (key == "format") {
            const auto f = value ? parse_format(*value) : std::nullopt;
            if (!f) {
                return ConfigError::BadFormat;
            }
            c.format = *f;
        } else if (key == "altitude_m") {
            const auto v = value ? parse_decimal(*value, limits::kMaxAltitudeM) : std::nullopt;
            if (!v) {
                return ConfigError::BadAltitude;
            }
            c.altitude_m = static_cast<std::uint16_t>(*v);
        } else if (key == "window") {
            const auto v = value ? parse_decimal(*value, limits::kMaxFilterWindow) : std::nullopt;
            if (!v) {
                return ConfigError::BadWindow;
            }
            c.filter_window = static_cast<std::uint8_t>(*v);
        } else {
            return ConfigError::UnknownField;
        }
    }
    const ConfigError err = validate(c);
    if (err == ConfigError::None) {
        base = c;
    }
    return err;
}

}  // namespace aq
