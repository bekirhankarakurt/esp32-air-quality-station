#include "aq/crc.hpp"

#include <array>
#include <cstring>

#include "test_framework.hpp"

TEST(crc8_sensirion_datasheet_vector) {
    const std::array<std::uint8_t, 2> beef{0xBE, 0xEF};
    CHECK_EQ(aq::crc8_sensirion(beef.data(), beef.size()), 0x92);
}

TEST(crc8_empty_is_init_value) { CHECK_EQ(aq::crc8_sensirion(nullptr, 0), 0xFF); }

TEST(crc8_is_constexpr) {
    constexpr std::array<std::uint8_t, 2> zero{0x00, 0x00};
    static_assert(aq::crc8_sensirion(zero.data(), zero.size()) == 0x81, "known value");
    CHECK(true);
}

TEST(crc32_check_value) {
    const char* s = "123456789";
    CHECK_EQ(aq::crc32(reinterpret_cast<const std::uint8_t*>(s), std::strlen(s)), 0xCBF43926U);
}

TEST(crc32_empty) { CHECK_EQ(aq::crc32(nullptr, 0), 0x00000000U); }

TEST(crc32_detects_single_bit_flip) {
    std::array<std::uint8_t, 32> buf{};
    for (std::size_t i = 0; i < buf.size(); ++i) {
        buf[i] = static_cast<std::uint8_t>(i * 7U);
    }
    const auto ref = aq::crc32(buf.data(), buf.size());
    for (std::size_t bit = 0; bit < buf.size() * 8; ++bit) {
        buf[bit / 8] ^= static_cast<std::uint8_t>(1U << (bit % 8));
        CHECK(aq::crc32(buf.data(), buf.size()) != ref);
        buf[bit / 8] ^= static_cast<std::uint8_t>(1U << (bit % 8));
    }
}

TEST_MAIN()
