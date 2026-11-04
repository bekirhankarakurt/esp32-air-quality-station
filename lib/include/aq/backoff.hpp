/// @file backoff.hpp
/// @brief Capped exponential backoff with "equal jitter".
#pragma once

#include <cstdint>

namespace aq {

/// Exponential backoff: the n-th delay is drawn from [d/2, d] where
/// d = min(max_ms, initial_ms * 2^n). The caller supplies the random value so
/// the class stays deterministic and testable (firmware passes esp_random()).
class Backoff {
public:
    constexpr Backoff(std::uint32_t initial_ms, std::uint32_t max_ms) noexcept
        : initial_ms_(initial_ms == 0 ? 1 : initial_ms), max_ms_(max_ms < initial_ms_ ? initial_ms_ : max_ms) {}

    /// Delay before the next attempt; advances the attempt counter.
    [[nodiscard]] std::uint32_t next(std::uint32_t random) noexcept;
    /// Upper bound of the next delay without advancing.
    [[nodiscard]] std::uint32_t ceiling() const noexcept;
    /// Call after a successful attempt.
    constexpr void reset() noexcept { attempts_ = 0; }

    [[nodiscard]] constexpr std::uint32_t attempts() const noexcept { return attempts_; }

private:
    std::uint32_t initial_ms_;
    std::uint32_t max_ms_;
    std::uint32_t attempts_{0};
};

}  // namespace aq
