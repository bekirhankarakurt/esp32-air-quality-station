// Tiny argument helpers shared by the host tools.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace aqhost {

/// Parse an unsigned decimal in [lo, hi].
[[nodiscard]] inline std::optional<std::uint64_t> parse_u64(std::string_view s, std::uint64_t lo, std::uint64_t hi) {
    if (s.empty() || s.size() > 19) {
        return std::nullopt;
    }
    std::uint64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        v = v * 10U + static_cast<std::uint64_t>(c - '0');
    }
    if (v < lo || v > hi) {
        return std::nullopt;
    }
    return v;
}

/// Parse a non-negative decimal fraction such as "0.05".
[[nodiscard]] inline std::optional<double> parse_fraction(std::string_view s, double hi) {
    if (s.empty() || s.size() > 16) {
        return std::nullopt;
    }
    char buf[32] = {};
    for (std::size_t i = 0; i < s.size(); ++i) {
        if ((s[i] < '0' || s[i] > '9') && s[i] != '.') {
            return std::nullopt;
        }
        buf[i] = s[i];
    }
    char* end = nullptr;
    const double v = std::strtod(buf, &end);
    if (end == buf || *end != '\0' || v < 0.0 || v > hi) {
        return std::nullopt;
    }
    return v;
}

}  // namespace aqhost
