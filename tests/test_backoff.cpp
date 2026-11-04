#include "aq/backoff.hpp"

#include "test_framework.hpp"

TEST(doubles_until_cap) {
    aq::Backoff b(500, 4000);
    CHECK_EQ(b.ceiling(), 500U);
    CHECK_EQ(b.next(250), 500U);  // half (250) + 250 % 251
    CHECK_EQ(b.ceiling(), 1000U);
    static_cast<void>(b.next(0));
    CHECK_EQ(b.ceiling(), 2000U);
    static_cast<void>(b.next(0));
    CHECK_EQ(b.ceiling(), 4000U);
    static_cast<void>(b.next(0));
    CHECK_EQ(b.ceiling(), 4000U);
    CHECK_EQ(b.attempts(), 4U);
}

TEST(equal_jitter_bounds) {
    aq::Backoff b(1000, 60000);
    for (std::uint32_t r = 0; r < 5000; r += 37) {
        aq::Backoff probe = b;
        const auto d = probe.next(r);
        CHECK(d >= 500U && d <= 1000U);
    }
}

TEST(reset_restarts) {
    aq::Backoff b(100, 1000);
    for (int i = 0; i < 10; ++i) {
        static_cast<void>(b.next(0));
    }
    b.reset();
    CHECK_EQ(b.attempts(), 0U);
    CHECK_EQ(b.ceiling(), 100U);
}

TEST(degenerate_parameters) {
    aq::Backoff zero(0, 0);
    CHECK_EQ(zero.ceiling(), 1U);
    const auto d = zero.next(12345);
    CHECK(d <= 1U);
    aq::Backoff inverted(5000, 10);
    CHECK_EQ(inverted.ceiling(), 5000U);
    aq::Backoff huge(1U << 31U, UINT32_MAX);
    for (int i = 0; i < 70; ++i) {
        static_cast<void>(huge.next(7));
    }
    CHECK_EQ(huge.ceiling(), UINT32_MAX);
}

TEST_MAIN()
