add_library(nativecore_runtime STATIC
  src/app.cpp
  src/platform_sdl.cpp
)

target_include_directories(nativecore_runtime PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nativecore_runtime PUBLIC nativecore_common)