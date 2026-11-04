#include "aq/moving_average.hpp"

#include "test_framework.hpp"

TEST(mean_of_partial_window) {
    aq::MovingAverage<8> avg(4);
    CHECK(avg.empty());
    CHECK_NEAR(avg.push(10.0F), 10.0, 1e-6);
    CHECK_NEAR(avg.push(20.0F), 15.0, 1e-6);
    CHECK_EQ(avg.count(), 2U);
    CHECK(!avg.full());
}

TEST(window_slides) {
    aq::MovingAverage<8> avg(3);
    for (const float v : {1.0F, 2.0F, 3.0F, 4.0F, 5.0F}) {
        avg.push(v);
    }
    CHECK(avg.full());
    CHECK_NEAR(avg.mean(), 4.0, 1e-6);
}

TEST(window_is_clamped) {
    aq::MovingAverage<4> big(100);
    CHECK_EQ(big.window(), 4U);
    aq::MovingAverage<4> zero(0);
    CHECK_EQ(zero.window(), 1U);
    zero.push(3.0F);
    CHECK_NEAR(zero.push(7.0F), 7.0, 1e-6);
}

TEST(reset_clears) {
    aq::MovingAverage<4> avg;
    avg.push(5.0F);
    avg.reset();
    CHECK(avg.empty());
    CHECK_NEAR(avg.push(1.0F), 1.0, 1e-6);
}

TEST(no_drift_over_long_runs) {
    aq::MovingAverage<16> avg(16);
    for (int i = 0; i < 1'000'000; ++i) {
        avg.push(i % 2 == 0 ? 400.1F : 1200.7F);
    }
    for (int i = 0; i < 16; ++i) {
        avg.push(21.5F);
    }
    CHECK_NEAR(avg.mean(), 21.5, 1e-5);
}

TEST_MAIN()
