cmake_minimum_required(VERSION 3.20)
project(NativeCore VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES 
      ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH OFF)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

find_package(SDL3 REQUIRED)
message(STATUS "Found SDL3 ${SDL3_VERSION}")
message(STATUS "Using SDLConfig.cmake in: ${SDL3_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(HandleLLVMOptions)
include(AddLLVM)

include(FetchContent)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        docking
  GIT_SHALLOW    TRUE
)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        yaml-cpp-0.9.0
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(yaml-cpp)

# ControllerImage - used for displaying buttons for controllers
FetchContent_Declare(
  ControllerImage
  GIT_REPOSITORY https://github.com/icculus/ControllerImage.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(ControllerImage)

add_library(ControllerImage STATIC
  ${ControllerImage_SOURCE_DIR}/src/controllerimage.c
)

target_include_directories(ControllerImage PUBLIC
  ${ControllerImage_SOURCE_DIR}/src
)

target_link_libraries(ControllerImage PUBLIC SDL3::SDL3)

# For Dear ImGui - build as a static library with SDL3+SDL_GPU backend
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_MakeAvailable(imgui)
endif()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC SDL3::SDL3)

# Build the ControllerImage data generator tool and produce the .bin
set(CONTROLLERIMAGE_BIN_DIR "${CMAKE_BINARY_DIR}/generated/controllerimage")
set(CONTROLLERIMAGE_BIN "${CONTROLLERIMAGE_BIN_DIR}/controllerimage-standard.bin")
set(CONTROLLERIMAGE_ART_DIR "${ControllerImage_SOURCE_DIR}/art")

file(MAKE_DIRECTORY "${CONTROLLERIMAGE_BIN_DIR}")

set(MAKE_TOOL_SRC "${ControllerImage_SOURCE_DIR}/src/make-controllerimage-data.c")
set(MAKE_TOOL_BIN "${CMAKE_BINARY_DIR}/generated/make-controllerimage-data")
execute_process(
  COMMAND ${CMAKE_C_COMPILER} "${MAKE_TOOL_SRC}" -o "${MAKE_TOOL_BIN}"
  RESULT_VARIABLE tool_build_result
)
if(NOT tool_build_result EQUAL 0)
  message(FATAL_ERROR "Failed to compile make-controllerimage-data")
endif()

# Run the tool to generate the .bin (only if not already up to date).
if(NOT EXISTS "${CONTROLLERIMAGE_BIN}")
  execute_process(
    COMMAND "${MAKE_TOOL_BIN}" "${CONTROLLERIMAGE_ART_DIR}"
    WORKING_DIRECTORY "${CONTROLLERIMAGE_BIN_DIR}"
    RESULT_VARIABLE tool_run_result
  )
  if(NOT tool_run_result EQUAL 0)
    message(FATAL_ERROR "make-controllerimage-data failed")
  endif()
  message(STATUS "Generated ${CONTROLLERIMAGE_BIN}")
endif()

add_subdirectory(common)
add_subdirectory(cores/gb)
add_subdirectory(runtime)
add_subdirectory(cli)
add_subdirectory(tests)
