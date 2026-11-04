/// @file air_quality.hpp
/// @brief Indoor air-quality index, comfort evaluation and dew point.
#pragma once

#include <cstdint>

namespace aq {

/// Qualitative band of the air-quality index.
enum class AirLevel : std::uint8_t {
    Good = 0,       ///< Index 0-50.
    Moderate = 1,   ///< Index 51-100.
    Poor = 2,       ///< Index 101-150: ventilate.
    Unhealthy = 3,  ///< Index 151-200.
    Hazardous = 4,  ///< Index > 200.
};

/// Lower-case name of a level ("good", "moderate", ...).
[[nodiscard]] const char* to_string(AirLevel level) noexcept;
/// Band containing @p index.
[[nodiscard]] AirLevel level_for_index(std::uint16_t index) noexcept;

/// Comfort band. Defaults follow common indoor guidance (ASHRAE 55 / EN 16798 style).
struct ComfortThresholds {
    float temp_min_c{20.0F};
    float temp_max_c{26.0F};
    float rh_min_pct{30.0F};
    float rh_max_pct{60.0F};
};

/// Evaluated air quality for one sample.
struct AirQuality {
    std::uint16_t index{0};       ///< 0 (best) .. 500 (worst); max of the sub-indices.
    AirLevel level{AirLevel::Good};
    bool comfortable{false};      ///< Temperature and humidity both inside the comfort band.
};

inline constexpr std::uint16_t kMaxIndex = 500;

/// CO2 sub-index, piecewise linear over the breakpoints
/// 400->0, 800->50, 1000->100, 1500->150, 2500->200, 5000->300, 10000->500.
[[nodiscard]] std::uint16_t co2_subindex(std::uint16_t co2_ppm) noexcept;
/// Thermal sub-index: 0 inside the band, +20 per degree outside, capped at 200.
[[nodiscard]] std::uint16_t temperature_subindex(float temperature_c, const ComfortThresholds& t) noexcept;
/// Humidity sub-index: 0 inside the band, +2.5 per %RH outside, capped at 200.
[[nodiscard]] std::uint16_t humidity_subindex(float humidity_pct, const ComfortThresholds& t) noexcept;

/// Combine available measurements. Invalid inputs are skipped; with no valid
/// input the result is index 0 / Good / not comfortable.
[[nodiscard]] AirQuality evaluate(bool thermo_valid, float temperature_c, float humidity_pct, bool co2_valid,
                                  std::uint16_t co2_ppm, const ComfortThresholds& thresholds = {}) noexcept;

/// Dew point in degrees Celsius (Magnus formula, valid -45..60 C).
[[nodiscard]] float dew_point_c(float temperature_c, float humidity_pct) noexcept;

}  // namespace aq
