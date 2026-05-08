# Portable core library (lib/) compiled as an ESP-IDF component.
set(core_dir "${CMAKE_CURRENT_LIST_DIR}/../../../lib")
idf_component_register(
  SRCS "${core_dir}/src/air_quality.cpp"
       "${core_dir}/src/backoff.cpp"
       "${core_dir}/src/config.cpp"
       "${core_dir}/src/crc.cpp"
       "${core_dir}/src/scd41.cpp"
       "${core_dir}/src/sensirion.cpp"
       "${core_dir}/src/sht31.cpp"
       "${core_dir}/src/station.cpp"
       "${core_dir}/src/telemetry.cpp"
  INCLUDE_DIRS "${core_dir}/include")
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17 -Wall -Wextra -Werror)
