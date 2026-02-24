#pragma once

#include <string>

namespace nativecore {

struct GBSettings {
  bool skip_boot_rom = true;
  bool enable_sound = true;
  std::string palette = "classic";
};

} // namespace nativecore
