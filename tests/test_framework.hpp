// Minimal dependency-free unit test harness.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace aqtest {

struct TestCase {
    const char* name;
    void (*fn)();
    TestCase* next;
};

inline TestCase*& registry() {
    static TestCase* head = nullptr;
    return head;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    explicit Registrar(TestCase* tc) {
        tc->next = registry();
        registry() = tc;
    }
};

inline void fail(const char* file, int line, const char* expr) {
    std::fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, expr);
    ++failures();
}

inline int run_all() {
    // Registration order is reversed; reverse back for readable output.
    TestCase* prev = nullptr;
    for (TestCase* tc = registry(); tc != nullptr;) {
        TestCase* next = tc->next;
        tc->next = prev;
        prev = tc;
        tc = next;
    }
    int run = 0;
    for (TestCase* tc = prev; tc != nullptr; tc = tc->next) {
        const int before = failures();
        tc->fn();
        std::printf("[%s] %s\n", failures() == before ? " ok " : "FAIL", tc->name);
        ++run;
    }
    std::printf("%d tests, %d failed checks\n", run, failures());
    return failures() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace aqtest

#define AQ_CAT2(a, b) a##b
#define AQ_CAT(a, b) AQ_CAT2(a, b)

#define TEST(name)                                                                     \
    static void name();                                                                \
    static aqtest::TestCase AQ_CAT(tc_, name){#name, &name, nullptr};                 \
    static const aqtest::Registrar AQ_CAT(reg_, name){&AQ_CAT(tc_, name)};             \
    static void name()

#define CHECK(expr)                                      \
    do {                                                 \
        if (!(expr)) {                                   \
            aqtest::fail(__FILE__, __LINE__, #expr);     \
        }                                                \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

#define CHECK_NEAR(a, b, tol) CHECK(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (tol))

#define TEST_MAIN() \
    int main() { return aqtest::run_all(); }
