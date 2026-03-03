add_executable(gb_test_runner
  test_runner.cpp
)

target_link_libraries(gb_test_runner PRIVATE nativecore_gb nativecore_common)

target_include_directories(gb_test_runner PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Download test ROMs from GitHub releases
include(FetchContent)
cmake_policy(SET CMP0135 NEW)

set(TEST_ROMS_VERSION "v7.0")
set(TEST_ROMS_URL "https://github.com/c-sp/game-boy-test-roms/releases/download/${TEST_ROMS_VERSION}/game-boy-test-roms-${TEST_ROMS_VERSION}.zip")

FetchContent_Declare(
  gb_test_roms
  URL ${TEST_ROMS_URL}
  SOURCE_DIR ${CMAKE_BINARY_DIR}/test-roms
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(gb_test_roms)

# Set the test ROM directory for use in CTest
set(TEST_ROM_DIR "${CMAKE_BINARY_DIR}/test-roms")

# Register as a CTest test
add_test(
  NAME gb_blargg_cpu_instrs
  COMMAND gb_test_runner --dir ${TEST_ROM_DIR} --filter blargg/cpu_instrs
)
set_tests_properties(gb_blargg_cpu_instrs PROPERTIES TIMEOUT 600)

add_test(
  NAME gb_blargg_instr_timing
  COMMAND gb_test_runner --dir ${TEST_ROM_DIR} --filter blargg/instr_timing
)
set_tests_properties(gb_blargg_instr_timing PROPERTIES TIMEOUT 300)

add_test(
  NAME gb_blargg_mem_timing
  COMMAND gb_test_runner --dir ${TEST_ROM_DIR} --filter blargg/mem_timing
)
set_tests_properties(gb_blargg_mem_timing PROPERTIES TIMEOUT 300)

add_test(
  NAME gb_mooneye_acceptance
  COMMAND gb_test_runner --dir ${TEST_ROM_DIR} --filter mooneye/acceptance
)
set_tests_properties(gb_mooneye_acceptance PROPERTIES TIMEOUT 300)

add_test(
  NAME gb_mooneye_emulator_only
  COMMAND gb_test_runner --dir ${TEST_ROM_DIR} --filter mooneye/emulator-only
)
set_tests_properties(gb_mooneye_emulator_only PROPERTIES TIMEOUT 300)