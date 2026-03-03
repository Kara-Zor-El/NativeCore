add_library(nativecore_gb STATIC
  src/cpu_lr35902.cpp
  src/ppu.cpp
  src/apu.cpp
  src/cartridge.cpp
  src/gb_core.cpp
  src/gb_recompiler.cpp
  src/gb_settings.cpp
)

target_include_directories(nativecore_gb PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include/gb
)

target_link_libraries(nativecore_gb PUBLIC nativecore_common)

add_subdirectory(tests)