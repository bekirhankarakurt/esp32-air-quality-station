/// @file rng.hpp
/// @brief Small deterministic PRNG (SplitMix64) for reproducible simulations.
#pragma once

#include <cmath>
#include <cstdint>

namespace aq::sim {

/// Deterministic across compilers and standard libraries (unlike std::normal_distribution).
class Rng {
public:
    explicit constexpr Rng(std::uint64_t seed) noexcept : state_(seed) {}

    /// SplitMix64 step.
    [[nodiscard]] constexpr std::uint64_t next_u64() noexcept {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    /// Uniform in [0, 1).
    [[nodiscard]] double uniform() noexcept { return static_cast<double>(next_u64() >> 11U) * 0x1.0p-53; }

    /// Standard normal (Box-Muller).
    [[nodiscard]] double normal() noexcept {
        const double u1 = 1.0 - uniform();
        const double u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    }

    /// True with probability @p p.
    [[nodiscard]] bool chance(double p) noexcept { return uniform() < p; }

private:
    std::uint64_t state_;
};

}  // namespace aq::sim
