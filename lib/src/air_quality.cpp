#include "aq/air_quality.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace aq {
namespace {

struct Breakpoint {
    float ppm;
    float index;
};

constexpr std::array<Breakpoint, 7> kCo2Breakpoints{{
    {400.0F, 0.0F},
    {800.0F, 50.0F},
    {1000.0F, 100.0F},
    {1500.0F, 150.0F},
    {2500.0F, 200.0F},
    {5000.0F, 300.0F},
    {10000.0F, 500.0F},
}};

constexpr float kSubindexCap = 200.0F;

std::uint16_t to_index(float v) noexcept {
    const float clamped = std::clamp(v, 0.0F, static_cast<float>(kMaxIndex));
    return static_cast<std::uint16_t>(std::lround(clamped));
}

float band_distance(float v, float lo, float hi) noexcept {
    if (v < lo) {
        return lo - v;
    }
    if (v > hi) {
        return v - hi;
    }
    return 0.0F;
}

}  // namespace

const char* to_string(AirLevel level) noexcept {
    switch (level) {
        case AirLevel::Good: return "good";
        case AirLevel::Moderate: return "moderate";
        case AirLevel::Poor: return "poor";
        case AirLevel::Unhealthy: return "unhealthy";
        case AirLevel::Hazardous: return "hazardous";
    }
    return "unknown";
}

AirLevel level_for_index(std::uint16_t index) noexcept {
    if (index <= 50) {
        return AirLevel::Good;
    }
    if (index <= 100) {
        return AirLevel::Moderate;
    }
    if (index <= 150) {
        return AirLevel::Poor;
    }
    if (index <= 200) {
        return AirLevel::Unhealthy;
    }
    return AirLevel::Hazardous;
}

std::uint16_t co2_subindex(std::uint16_t co2_ppm) noexcept {
    const auto ppm = static_cast<float>(co2_ppm);
    if (ppm <= kCo2Breakpoints.front().ppm) {
        return 0;
    }
    for (std::size_t i = 1; i < kCo2Breakpoints.size(); ++i) {
        const Breakpoint& lo = kCo2Breakpoints[i - 1];
        const Breakpoint& hi = kCo2Breakpoints[i];
        if (ppm <= hi.ppm) {
            return to_index(lo.index + (hi.index - lo.index) * (ppm - lo.ppm) / (hi.ppm - lo.ppm));
        }
    }
    return kMaxIndex;
}

std::uint16_t temperature_subindex(float temperature_c, const ComfortThresholds& t) noexcept {
    return to_index(std::min(kSubindexCap, 20.0F * band_distance(temperature_c, t.temp_min_c, t.temp_max_c)));
}

std::uint16_t humidity_subindex(float humidity_pct, const ComfortThresholds& t) noexcept {
    return to_index(std::min(kSubindexCap, 2.5F * band_distance(humidity_pct, t.rh_min_pct, t.rh_max_pct)));
}

AirQuality evaluate(bool thermo_valid, float temperature_c, float humidity_pct, bool co2_valid, std::uint16_t co2_ppm,
                    const ComfortThresholds& thresholds) noexcept {
    AirQuality q{};
    if (thermo_valid) {
        q.index = std::max(temperature_subindex(temperature_c, thresholds),
                           humidity_subindex(humidity_pct, thresholds));
        q.comfortable = band_distance(temperature_c, thresholds.temp_min_c, thresholds.temp_max_c) == 0.0F &&
                        band_distance(humidity_pct, thresholds.rh_min_pct, thresholds.rh_max_pct) == 0.0F;
    }
    if (co2_valid) {
        q.index = std::max(q.index, co2_subindex(co2_ppm));
    }
    q.level = level_for_index(q.index);
    return q;
}

float dew_point_c(float temperature_c, float humidity_pct) noexcept {
    constexpr float kA = 17.62F;
    constexpr float kB = 243.12F;
    const float rh = std::clamp(humidity_pct, 0.1F, 100.0F);
    const float gamma = std::log(rh / 100.0F) + kA * temperature_c / (kB + temperature_c);
    return kB * gamma / (kA - gamma);
}

}  // namespace aq
