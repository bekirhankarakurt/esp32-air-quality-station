#include "aq/air_quality.hpp"

#include <string_view>

#include "test_framework.hpp"

TEST(co2_breakpoints) {
    CHECK_EQ(aq::co2_subindex(0), 0);
    CHECK_EQ(aq::co2_subindex(400), 0);
    CHECK_EQ(aq::co2_subindex(600), 25);
    CHECK_EQ(aq::co2_subindex(800), 50);
    CHECK_EQ(aq::co2_subindex(900), 75);
    CHECK_EQ(aq::co2_subindex(1000), 100);
    CHECK_EQ(aq::co2_subindex(1500), 150);
    CHECK_EQ(aq::co2_subindex(2500), 200);
    CHECK_EQ(aq::co2_subindex(5000), 300);
    CHECK_EQ(aq::co2_subindex(10000), 500);
    CHECK_EQ(aq::co2_subindex(40000), 500);
}

TEST(co2_subindex_is_monotonic) {
    std::uint16_t prev = 0;
    for (std::uint32_t ppm = 0; ppm <= 12000; ppm += 10) {
        const auto idx = aq::co2_subindex(static_cast<std::uint16_t>(ppm));
        CHECK(idx >= prev);
        prev = idx;
    }
}

TEST(thermal_and_humidity_subindices) {
    const aq::ComfortThresholds t{};
    CHECK_EQ(aq::temperature_subindex(22.0F, t), 0);
    CHECK_EQ(aq::temperature_subindex(20.0F, t), 0);
    CHECK_EQ(aq::temperature_subindex(28.5F, t), 50);
    CHECK_EQ(aq::temperature_subindex(17.0F, t), 60);
    CHECK_EQ(aq::temperature_subindex(-20.0F, t), 200);
    CHECK_EQ(aq::humidity_subindex(45.0F, t), 0);
    CHECK_EQ(aq::humidity_subindex(80.0F, t), 50);
    CHECK_EQ(aq::humidity_subindex(10.0F, t), 50);
}

TEST(levels) {
    CHECK(aq::level_for_index(0) == aq::AirLevel::Good);
    CHECK(aq::level_for_index(50) == aq::AirLevel::Good);
    CHECK(aq::level_for_index(51) == aq::AirLevel::Moderate);
    CHECK(aq::level_for_index(101) == aq::AirLevel::Poor);
    CHECK(aq::level_for_index(200) == aq::AirLevel::Unhealthy);
    CHECK(aq::level_for_index(201) == aq::AirLevel::Hazardous);
    CHECK(std::string_view(aq::to_string(aq::AirLevel::Poor)) == "poor");
    CHECK(std::string_view(aq::to_string(aq::AirLevel::Hazardous)) == "hazardous");
}

TEST(evaluate_takes_worst_subindex) {
    const auto q = aq::evaluate(true, 22.0F, 45.0F, true, 1200, {});
    CHECK_EQ(q.index, 120);
    CHECK(q.level == aq::AirLevel::Poor);
    CHECK(q.comfortable);

    const auto hot = aq::evaluate(true, 30.0F, 45.0F, true, 500, {});
    CHECK_EQ(hot.index, 80);
    CHECK(!hot.comfortable);
}

TEST(evaluate_skips_invalid_inputs) {
    const auto none = aq::evaluate(false, 0.0F, 0.0F, false, 0, {});
    CHECK_EQ(none.index, 0);
    CHECK(!none.comfortable);
    const auto co2_only = aq::evaluate(false, 99.0F, 0.0F, true, 2500, {});
    CHECK_EQ(co2_only.index, 200);
    const auto thermo_only = aq::evaluate(true, 22.0F, 45.0F, false, 9000, {});
    CHECK_EQ(thermo_only.index, 0);
}

TEST(custom_thresholds) {
    const aq::ComfortThresholds strict{21.0F, 23.0F, 40.0F, 50.0F};
    const auto q = aq::evaluate(true, 24.0F, 45.0F, false, 0, strict);
    CHECK_EQ(q.index, 20);
    CHECK(!q.comfortable);
}

TEST(dew_point_reference_values) {
    // Reference values from the Magnus formula (Sensirion application note).
    CHECK_NEAR(aq::dew_point_c(25.0F, 50.0F), 13.85, 0.05);
    CHECK_NEAR(aq::dew_point_c(20.0F, 100.0F), 20.0, 0.01);
    CHECK_NEAR(aq::dew_point_c(0.0F, 80.0F), -3.0, 0.1);
    CHECK(aq::dew_point_c(25.0F, 0.0F) < -40.0F);
}

TEST_MAIN()
