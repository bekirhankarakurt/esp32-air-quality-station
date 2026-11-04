#include "aq/telemetry.hpp"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "aq/crc.hpp"

namespace aq {
namespace {

void put_u16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v >> 8U);
    p[1] = static_cast<std::uint8_t>(v);
}

void put_u32(std::uint8_t* p, std::uint32_t v) noexcept {
    for (int i = 3; i >= 0; --i) {
        p[i] = static_cast<std::uint8_t>(v);
        v >>= 8U;
    }
}

void put_u64(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 7; i >= 0; --i) {
        p[i] = static_cast<std::uint8_t>(v);
        v >>= 8U;
    }
}

void put_f32(std::uint8_t* p, float v) noexcept {
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "float must be IEEE-754 binary32");
    std::memcpy(&bits, &v, sizeof(bits));
    put_u32(p, bits);
}

std::uint16_t get_u16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8U) | p[1]);
}

std::uint32_t get_u32(const std::uint8_t* p) noexcept {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v = (v << 8U) | p[i];
    }
    return v;
}

std::uint64_t get_u64(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8U) | p[i];
    }
    return v;
}

float get_f32(const std::uint8_t* p) noexcept {
    const std::uint32_t bits = get_u32(p);
    float v = 0.0F;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

bool same_bits(float a, float b) noexcept { return std::memcmp(&a, &b, sizeof(float)) == 0; }

constexpr std::uint8_t kMaxLevel = static_cast<std::uint8_t>(AirLevel::Hazardous);

// ---- minimal flat JSON object scanner -------------------------------------

class JsonScanner {
public:
    explicit JsonScanner(std::string_view s) noexcept : s_(s) {}

    void skip_ws() noexcept {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n')) {
            ++pos_;
        }
    }

    [[nodiscard]] bool consume(char c) noexcept {
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool peek(char c) noexcept {
        skip_ws();
        return pos_ < s_.size() && s_[pos_] == c;
    }

    /// Quoted string without escapes (the encoder never emits any).
    [[nodiscard]] std::optional<std::string_view> string() noexcept {
        if (!consume('"')) {
            return std::nullopt;
        }
        const std::size_t start = pos_;
        while (pos_ < s_.size() && s_[pos_] != '"') {
            if (s_[pos_] == '\\') {
                return std::nullopt;
            }
            ++pos_;
        }
        if (pos_ >= s_.size()) {
            return std::nullopt;
        }
        return s_.substr(start, pos_++ - start);
    }

    /// Raw number token, or empty view.
    [[nodiscard]] std::string_view number() noexcept {
        skip_ws();
        const std::size_t start = pos_;
        while (pos_ < s_.size() && std::strchr("+-.0123456789eE", s_[pos_]) != nullptr && s_[pos_] != '\0') {
            ++pos_;
        }
        return s_.substr(start, pos_ - start);
    }

    [[nodiscard]] bool literal(std::string_view word) noexcept {
        skip_ws();
        if (s_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    [[nodiscard]] bool at_end() noexcept {
        skip_ws();
        return pos_ == s_.size();
    }

private:
    std::string_view s_;
    std::size_t pos_{0};
};

/// Copy a token into a NUL-terminated buffer for strto*.
template <std::size_t N>
bool to_cstr(std::string_view tok, std::array<char, N>& buf) noexcept {
    if (tok.empty() || tok.size() >= N) {
        return false;
    }
    std::memcpy(buf.data(), tok.data(), tok.size());
    buf[tok.size()] = '\0';
    return true;
}

std::optional<std::uint64_t> parse_uint(std::string_view tok, std::uint64_t max) noexcept {
    std::array<char, 24> buf{};
    if (!to_cstr(tok, buf) || buf[0] == '-' || buf[0] == '+') {
        return std::nullopt;
    }
    char* end = nullptr;
    const unsigned long long v = std::strtoull(buf.data(), &end, 10);
    if (end == nullptr || *end != '\0' || v > max) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(v);
}

std::optional<std::uint64_t> parse_hex(std::string_view tok, std::uint64_t max) noexcept {
    std::array<char, 24> buf{};
    if (!to_cstr(tok, buf)) {
        return std::nullopt;
    }
    for (std::size_t i = 0; buf[i] != '\0'; ++i) {
        if (std::strchr("0123456789abcdefABCDEF", buf[i]) == nullptr) {
            return std::nullopt;
        }
    }
    char* end = nullptr;
    const unsigned long long v = std::strtoull(buf.data(), &end, 16);
    if (end == nullptr || *end != '\0' || v > max) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(v);
}

std::optional<float> parse_float(std::string_view tok) noexcept {
    std::array<char, 32> buf{};
    if (!to_cstr(tok, buf)) {
        return std::nullopt;
    }
    char* end = nullptr;
    const float v = std::strtof(buf.data(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(v)) {
        return std::nullopt;
    }
    return v;
}

std::optional<AirLevel> parse_level(std::string_view s) noexcept {
    for (std::uint8_t i = 0; i <= kMaxLevel; ++i) {
        const auto level = static_cast<AirLevel>(i);
        if (s == to_string(level)) {
            return level;
        }
    }
    return std::nullopt;
}

}  // namespace

bool operator==(const Reading& a, const Reading& b) noexcept {
    return a.seq == b.seq && a.timestamp_ns == b.timestamp_ns && a.device_id == b.device_id &&
           same_bits(a.temperature_c, b.temperature_c) && same_bits(a.humidity_pct, b.humidity_pct) &&
           same_bits(a.dew_point_c, b.dew_point_c) && a.co2_ppm == b.co2_ppm && a.aqi == b.aqi &&
           a.level == b.level && a.flags == b.flags && a.error_count == b.error_count;
}

const char* to_string(DecodeError e) noexcept {
    switch (e) {
        case DecodeError::None: return "ok";
        case DecodeError::TooShort: return "too short";
        case DecodeError::BadMagic: return "bad magic";
        case DecodeError::BadVersion: return "bad version";
        case DecodeError::BadType: return "bad type";
        case DecodeError::BadLength: return "bad length";
        case DecodeError::BadCrc: return "CRC mismatch";
        case DecodeError::BadValue: return "bad value";
    }
    return "unknown";
}

Packet encode_binary(const Reading& r) noexcept {
    Packet pkt{};
    std::uint8_t* p = pkt.data();
    put_u32(p + 0, wire::kMagic);
    p[4] = wire::kVersion;
    p[5] = static_cast<std::uint8_t>(MessageType::AirQuality);
    put_u16(p + 6, static_cast<std::uint16_t>(wire::kPayloadSize));
    put_u64(p + 8, r.seq);
    put_u64(p + 16, r.timestamp_ns);
    std::uint8_t* q = p + wire::kHeaderSize;
    put_u32(q + 0, r.device_id);
    put_f32(q + 4, r.temperature_c);
    put_f32(q + 8, r.humidity_pct);
    put_f32(q + 12, r.dew_point_c);
    put_u16(q + 16, r.co2_ppm);
    put_u16(q + 18, r.aqi);
    q[20] = static_cast<std::uint8_t>(r.level);
    q[21] = r.flags;
    put_u16(q + 22, r.error_count);
    put_u32(p + wire::kHeaderSize + wire::kPayloadSize, crc32(p, wire::kHeaderSize + wire::kPayloadSize));
    return pkt;
}

DecodeResult decode_binary(const std::uint8_t* data, std::size_t len) noexcept {
    DecodeResult res{};
    if (data == nullptr || len < wire::kHeaderSize) {
        res.error = DecodeError::TooShort;
        return res;
    }
    if (get_u32(data) != wire::kMagic) {
        res.error = DecodeError::BadMagic;
    } else if (data[4] != wire::kVersion) {
        res.error = DecodeError::BadVersion;
    } else if (data[5] != static_cast<std::uint8_t>(MessageType::AirQuality)) {
        res.error = DecodeError::BadType;
    } else if (get_u16(data + 6) != wire::kPayloadSize || len != wire::kPacketSize) {
        res.error = DecodeError::BadLength;
    } else if (get_u32(data + wire::kHeaderSize + wire::kPayloadSize) !=
               crc32(data, wire::kHeaderSize + wire::kPayloadSize)) {
        res.error = DecodeError::BadCrc;
    } else if (data[wire::kHeaderSize + 20] > kMaxLevel) {
        res.error = DecodeError::BadValue;
    }
    if (!res.ok()) {
        return res;
    }
    Reading& r = res.reading;
    r.seq = get_u64(data + 8);
    r.timestamp_ns = get_u64(data + 16);
    const std::uint8_t* q = data + wire::kHeaderSize;
    r.device_id = get_u32(q + 0);
    r.temperature_c = get_f32(q + 4);
    r.humidity_pct = get_f32(q + 8);
    r.dew_point_c = get_f32(q + 12);
    r.co2_ppm = get_u16(q + 16);
    r.aqi = get_u16(q + 18);
    r.level = static_cast<AirLevel>(q[20]);
    r.flags = q[21];
    r.error_count = get_u16(q + 22);
    return res;
}

std::size_t encode_json(const Reading& r, char* out, std::size_t capacity) noexcept {
    if (out == nullptr || capacity == 0) {
        return 0;
    }
    std::array<char, 16> temp{"null"};
    std::array<char, 16> rh{"null"};
    std::array<char, 16> dew{"null"};
    std::array<char, 8> co2{"null"};
    if (r.has(flags::kThermoValid)) {
        std::snprintf(temp.data(), temp.size(), "%.2f", static_cast<double>(r.temperature_c));
        std::snprintf(rh.data(), rh.size(), "%.2f", static_cast<double>(r.humidity_pct));
        std::snprintf(dew.data(), dew.size(), "%.2f", static_cast<double>(r.dew_point_c));
    }
    if (r.has(flags::kCo2Valid)) {
        std::snprintf(co2.data(), co2.size(), "%u", static_cast<unsigned>(r.co2_ppm));
    }
    const int n = std::snprintf(
        out, capacity,
        "{\"type\":\"air\",\"seq\":%" PRIu64 ",\"ts_ns\":%" PRIu64 ",\"device\":\"%08" PRIX32
        "\",\"temp_c\":%s,\"rh_pct\":%s,\"dew_c\":%s,\"co2_ppm\":%s,\"aqi\":%u,\"level\":\"%s\",\"flags\":%u,"
        "\"errors\":%u}\n",
        r.seq, r.timestamp_ns, r.device_id, temp.data(), rh.data(), dew.data(), co2.data(),
        static_cast<unsigned>(r.aqi), to_string(r.level), static_cast<unsigned>(r.flags),
        static_cast<unsigned>(r.error_count));
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n);
}

std::optional<Reading> parse_json(std::string_view line) noexcept {
    JsonScanner sc(line);
    if (!sc.consume('{')) {
        return std::nullopt;
    }
    Reading r{};
    // Bit per mandatory key, set when seen.
    enum : std::uint16_t {
        kType = 1U << 0U, kSeq = 1U << 1U, kTs = 1U << 2U, kDev = 1U << 3U, kTemp = 1U << 4U, kRh = 1U << 5U,
        kDew = 1U << 6U, kCo2 = 1U << 7U, kAqi = 1U << 8U, kLevel = 1U << 9U, kFlags = 1U << 10U,
        kErrors = 1U << 11U, kAll = (1U << 12U) - 1U
    };
    std::uint16_t seen = 0;
    bool temp_null = false;
    bool co2_null = false;
    bool first = true;
    while (!sc.peek('}')) {
        if (!first && !sc.consume(',')) {
            return std::nullopt;
        }
        first = false;
        const auto key = sc.string();
        if (!key || !sc.consume(':')) {
            return std::nullopt;
        }
        const std::string_view k = *key;
        if (k == "type" || k == "device" || k == "level") {
            const auto v = sc.string();
            if (!v) {
                return std::nullopt;
            }
            if (k == "type") {
                if (*v != "air") {
                    return std::nullopt;
                }
                seen |= kType;
            } else if (k == "device") {
                const auto id = parse_hex(*v, UINT32_MAX);
                if (!id) {
                    return std::nullopt;
                }
                r.device_id = static_cast<std::uint32_t>(*id);
                seen |= kDev;
            } else {
                const auto lvl = parse_level(*v);
                if (!lvl) {
                    return std::nullopt;
                }
                r.level = *lvl;
                seen |= kLevel;
            }
            continue;
        }
        const bool is_null = sc.literal("null");
        const std::string_view tok = is_null ? std::string_view{} : sc.number();
        if (k == "temp_c" || k == "rh_pct" || k == "dew_c") {
            float v = NAN;
            if (!is_null) {
                const auto f = parse_float(tok);
                if (!f) {
                    return std::nullopt;
                }
                v = *f;
            }
            if (k == "temp_c") {
                r.temperature_c = v;
                temp_null = is_null;
                seen |= kTemp;
            } else if (k == "rh_pct") {
                r.humidity_pct = v;
                seen |= kRh;
            } else {
                r.dew_point_c = v;
                seen |= kDew;
            }
            continue;
        }
        if (k == "co2_ppm" && is_null) {
            co2_null = true;
            seen |= kCo2;
            continue;
        }
        if (is_null) {
            return std::nullopt;
        }
        if (k == "seq" || k == "ts_ns") {
            const auto v = parse_uint(tok, UINT64_MAX);
            if (!v) {
                return std::nullopt;
            }
            (k == "seq" ? r.seq : r.timestamp_ns) = *v;
            seen |= (k == "seq" ? kSeq : kTs);
        } else if (k == "co2_ppm" || k == "aqi" || k == "errors") {
            const auto v = parse_uint(tok, UINT16_MAX);
            if (!v) {
                return std::nullopt;
            }
            const auto u = static_cast<std::uint16_t>(*v);
            if (k == "co2_ppm") {
                r.co2_ppm = u;
                seen |= kCo2;
            } else if (k == "aqi") {
                r.aqi = u;
                seen |= kAqi;
            } else {
                r.error_count = u;
                seen |= kErrors;
            }
        } else if (k == "flags") {
            const auto v = parse_uint(tok, UINT8_MAX);
            if (!v) {
                return std::nullopt;
            }
            r.flags = static_cast<std::uint8_t>(*v);
            seen |= kFlags;
        } else {
            return std::nullopt;
        }
    }
    if (!sc.consume('}') || !sc.at_end() || seen != kAll) {
        return std::nullopt;
    }
    if (temp_null == r.has(flags::kThermoValid) || co2_null == r.has(flags::kCo2Valid)) {
        return std::nullopt;
    }
    return r;
}

bool looks_like_json(const std::uint8_t* data, std::size_t len) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        const auto c = static_cast<char>(data[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }
        return c == '{';
    }
    return false;
}

}  // namespace aq
