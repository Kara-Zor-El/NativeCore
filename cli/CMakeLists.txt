add_executable(nativecore-cli src/main.cpp)

target_link_libraries(nativecore-cli PRIVATE
  nativecore_common
  nativecore_gb
  nativecore_runtime
)