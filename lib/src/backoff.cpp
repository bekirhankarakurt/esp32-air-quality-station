#include "aq/backoff.hpp"

namespace aq {

std::uint32_t Backoff::ceiling() const noexcept {
    std::uint64_t d = initial_ms_;
    for (std::uint32_t i = 0; i < attempts_ && d < max_ms_; ++i) {
        d *= 2U;
    }
    return d > max_ms_ ? max_ms_ : static_cast<std::uint32_t>(d);
}

std::uint32_t Backoff::next(std::uint32_t random) noexcept {
    const std::uint32_t d = ceiling();
    if (attempts_ < UINT32_MAX) {
        ++attempts_;
    }
    const std::uint32_t half = d / 2U;
    return half + random % (d - half + 1U);
}

}  // namespace aq
