# Contributing

Thanks for your interest! Bug reports, hardware test results and pull requests are welcome.

## Development setup

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DAQ_SANITIZE=ON -DAQ_WERROR=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
scripts/smoke_test.sh build
```

Firmware (ESP-IDF v5.3.1, or the `espressif/idf:v5.3.1` Docker image):

```bash
cd firmware && idf.py set-target esp32 && idf.py build
```

## Guidelines

- C++17, no exceptions and no heap allocation in `lib/` or the firmware's sampling path.
- Put hardware-independent logic in `lib/` and cover it with a unit test in `tests/`.
- Use `[[nodiscard]]` for results, `enum class` for states, RAII for resources, and `const` wherever it applies.
- Code must build without warnings (`-Wall -Wextra -Wpedantic`) and pass cppcheck and clang-tidy (`.clang-tidy`):

  ```bash
  cppcheck --enable=warning,style,performance,portability --std=c++17 --error-exitcode=1 \
    --suppress=missingIncludeSystem --suppress=unusedFunction \
    -I lib/include -I sim/include -I apps/common --quiet lib/src sim/src apps
  clang-tidy -p build --quiet lib/src/*.cpp sim/src/*.cpp apps/*/main.cpp
  ```

- If you change the wire format, bump `wire::kVersion` and update the tables in the README.
- Write commit messages in the imperative mood ("Add SGP41 driver") and keep each commit to one logical change.
- Add an entry under `[Unreleased]` in `CHANGELOG.md`.
