find_package(Slang REQUIRED)

add_library(nativecore_common STATIC
  src/audio.cpp
  src/config.cpp
  src/frame_limiter.cpp
  src/input.cpp
  src/ir.cpp
  src/recompiler.cpp
  src/core.cpp
  src/settings_overlay.cpp
  src/themes/theme_default.cpp
  src/themes/theme_game_boy.cpp
  src/video.cpp
)

target_include_directories(nativecore_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${LLVM_INCLUDE_DIRS}
)

target_compile_definitions(nativecore_common PUBLIC ${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(LLVM_LIBS
  core
  support
  irreader
  orcjit
  native
  ${LLVM_TARGETS_TO_BUILD}
)

target_link_libraries(nativecore_common PUBLIC
  SDL3::SDL3
  imgui
  yaml-cpp
  ${LLVM_LIBS}
)