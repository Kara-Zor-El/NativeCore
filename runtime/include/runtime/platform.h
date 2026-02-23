#pragma once

#include <string>

namespace nativecore::runtime {

struct PlatformInfo {
  std::string os_name;
  std::string arch;
  bool is_macos = false;
  bool is_linux = false;
  bool is_windows = false;
};

PlatformInfo getPlatformInfo();

std::string getConfigDirectory(const std::string &app_name);

} // namespace nativecore::runtime