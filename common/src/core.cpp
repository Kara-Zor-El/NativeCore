#include "nativecore/core.h"

namespace nativecore::GameBoy {
std::unique_ptr<Core> createGameBoyCore();
}

namespace nativecore {

std::unique_ptr<Core> createCore(SystemType system) {
  switch (system) {
  case SystemType::GameBoy:
    return nativecore::GameBoy::createGameBoyCore();
  default:
    throw std::runtime_error("Unsupported system type: " +
                             std::to_string(static_cast<int>(system)));
  }
}

SystemType detectSystem(const std::vector<uint8_t> &rom_data) {
  if (rom_data.size() < 16)
    return SystemType::Unknown;

  // Check for Game Boy ROM signature
  if (rom_data.size() >= 0x0150) {
    static const uint8_t nintendo_logo[] = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
        0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    };
    bool logo_match = true;
    for (int i = 0; i < 16; i++) {
      if (rom_data[0x0104 + i] != nintendo_logo[i]) {
        logo_match = false;
        break;
      }
    }
    if (logo_match) {
      // NOTE: Game Boy Color also uses this method
      // Support will be added later
      return SystemType::GameBoy;
    }
  }
  return SystemType::Unknown;
}

} // namespace nativecore