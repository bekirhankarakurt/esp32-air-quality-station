/// @file moving_average.hpp
/// @brief Fixed-capacity simple moving average (no heap allocation).
#pragma once

#include <array>
#include <cstddef>

namespace aq {

/// Simple moving average over the last `window()` samples.
/// Storage is a fixed array of @p Capacity elements; the active window is
/// chosen at runtime (1..Capacity). The sum is recomputed on every push, so
/// floating-point error never accumulates over long uptimes.
template <std::size_t Capacity>
class MovingAverage {
    static_assert(Capacity > 0, "capacity must be positive");

public:
    explicit constexpr MovingAverage(std::size_t window = Capacity) noexcept
        : window_(window == 0 ? 1 : (window > Capacity ? Capacity : window)) {}

    /// Add a sample and return the updated mean.
    float push(float sample) noexcept {
        buf_[head_] = sample;
        head_ = (head_ + 1) % window_;
        if (count_ < window_) {
            ++count_;
        }
        double sum = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            sum += static_cast<double>(buf_[i]);
        }
        mean_ = static_cast<float>(sum / static_cast<double>(count_));
        return mean_;
    }

    /// Forget all samples.
    constexpr void reset() noexcept {
        head_ = 0;
        count_ = 0;
        mean_ = 0.0F;
    }

    [[nodiscard]] constexpr float mean() const noexcept { return mean_; }
    [[nodiscard]] constexpr std::size_t count() const noexcept { return count_; }
    [[nodiscard]] constexpr std::size_t window() const noexcept { return window_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] constexpr bool full() const noexcept { return count_ == window_; }

private:
    std::array<float, Capacity> buf_{};
    std::size_t window_;
    std::size_t head_{0};
    std::size_t count_{0};
    float mean_{0.0F};
};

}  // namespace aq
