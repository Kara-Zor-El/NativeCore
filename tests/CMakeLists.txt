include(FetchContent)
cmake_policy(SET CMP0135 NEW)

add_library(nativecore_test_harness STATIC
  src/test_runner.cpp
)

target_include_directories(nativecore_test_harness PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(nativecore_test_harness PUBLIC
  nativecore_common
)

set(TEST_ROMS_VERSION "v7.0")
set(TEST_ROMS_URL
  "https://github.com/c-sp/game-boy-test-roms/releases/download/${TEST_ROMS_VERSION}/game-boy-test-roms-${TEST_ROMS_VERSION}.zip"
)

FetchContent_Declare(
  gb_test_roms
  URL ${TEST_ROMS_URL}
  SOURCE_DIR ${CMAKE_BINARY_DIR}/test-roms
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(gb_test_roms)

macro(add_gb_test target_name source_file ctest_name timeout_seconds)
  add_executable(${target_name}
    gb/${source_file}
  )
  target_include_directories(${target_name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gb
    ${CMAKE_SOURCE_DIR}/cores/gb/include
    ${CMAKE_SOURCE_DIR}/cores/gb/include/gb
  )
  target_link_libraries(${target_name} PRIVATE
    nativecore_test_harness
    nativecore_gb
  )
  add_test(NAME ${ctest_name} COMMAND ${target_name})
  set_tests_properties(${ctest_name} PROPERTIES TIMEOUT ${timeout_seconds})
endmacro()

# GB test suite executables
add_gb_test(gb_blargg_cpu_instrs    blargg_cpu_instrs.cpp    gb_blargg_cpu_instrs    600)
add_gb_test(gb_blargg_instr_timing  blargg_instr_timing.cpp  gb_blargg_instr_timing  300)
add_gb_test(gb_blargg_mem_timing    blargg_mem_timing.cpp    gb_blargg_mem_timing    300)
add_gb_test(gb_blargg_mem_timing2   blargg_mem_timing2.cpp   gb_blargg_mem_timing2   300)
add_gb_test(gb_blargg_halt_bug      blargg_halt_bug.cpp      gb_blargg_halt_bug      300)
add_gb_test(gb_blargg_oam_bug       blargg_oam_bug.cpp       gb_blargg_oam_bug       300)
add_gb_test(gb_mooneye_acceptance   mooneye_acceptance.cpp   gb_mooneye_acceptance   300)
add_gb_test(gb_mooneye_emulator_only mooneye_emulator_only.cpp gb_mooneye_emulator_only 300)
