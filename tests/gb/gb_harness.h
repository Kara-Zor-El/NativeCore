#pragma once

#include "gb_core.h"
#include "test_runner.h"

#include <filesystem>
#include <string>
#include <vector>

namespace gb_tests {

// Locate the test-roms directory
// FetchContent places it at <build_dir>/test-roms/.
inline std::string findTestRomDir() {
  namespace fs = std::filesystem;
  auto dir = fs::current_path();
  // walk up the directory tree if not found
  for (int i = 0; i < 6; i++) {
    fs::path candidate = dir / fs::path("test-roms");
    if (fs::is_directory(candidate))
      return candidate.string();
    if (dir.has_parent_path())
      dir = dir.parent_path();
    else
      break;
  }
  return "";
}

// Blacklist of ROM filenames that target hardware revisions we don't emulate.
inline const std::vector<std::string> &mooneyeBlacklist() {
  static const std::vector<std::string> bl = {
      "boot_div-S.gb",    "boot_div-dmg0.gb",  "boot_div2-S.gb",
      "boot_hwio-S.gb",   "boot_hwio-dmg0.gb", "boot_regs-dmg0.gb",
      "boot_regs-mgb.gb", "boot_regs-sgb.gb",  "boot_regs-sgb2.gb",
  };
  return bl;
}

// Mooneye pass pattern: Fibonacci sequence in B/C/D/E/H/L (3,5,8,13,21,34).
inline nativecore::testing::RegisterPattern mooneyePassPattern() {
  nativecore::testing::RegisterPattern p;
  p.regs = {
      {static_cast<int>(nativecore::GBReg::B), 3},
      {static_cast<int>(nativecore::GBReg::C), 5},
      {static_cast<int>(nativecore::GBReg::D), 8},
      {static_cast<int>(nativecore::GBReg::E), 13},
      {static_cast<int>(nativecore::GBReg::H), 21},
      {static_cast<int>(nativecore::GBReg::L), 34},
  };
  return p;
}

// Mooneye fail pattern: all of B/C/D/E/H/L = 0x42.
inline nativecore::testing::RegisterPattern mooneyeFailPattern() {
  nativecore::testing::RegisterPattern p;
  p.regs = {
      {static_cast<int>(nativecore::GBReg::B), 0x42},
      {static_cast<int>(nativecore::GBReg::C), 0x42},
      {static_cast<int>(nativecore::GBReg::D), 0x42},
      {static_cast<int>(nativecore::GBReg::E), 0x42},
      {static_cast<int>(nativecore::GBReg::H), 0x42},
      {static_cast<int>(nativecore::GBReg::L), 0x42},
  };
  return p;
}

// Build a Blargg runner for a given ROM path.
inline auto makeBlarggRunner(int timeout_seconds = 120) {
  return [timeout_seconds](
             const std::string &rom_path) -> nativecore::testing::TestOutcome {
    auto rom_data = nativecore::testing::loadFile(rom_path);
    if (rom_data.empty())
      return {nativecore::testing::TestResult::Error,
              "Failed to load ROM: " + rom_path, 0.0};

    auto core = std::make_unique<nativecore::GBCore>();
    if (!core->loadROM(rom_data))
      return {nativecore::testing::TestResult::Error,
              "Failed to parse ROM: " + rom_path, 0.0};

    return nativecore::testing::runSerialOutputTest(*core, *core,
                                                    timeout_seconds);
  };
}

// Build a Mooneye runner for a given ROM path.
inline auto makeMooneyeRunner(int timeout_seconds = 30) {
  return [timeout_seconds](
             const std::string &rom_path) -> nativecore::testing::TestOutcome {
    auto rom_data = nativecore::testing::loadFile(rom_path);
    if (rom_data.empty())
      return {nativecore::testing::TestResult::Error,
              "Failed to load ROM: " + rom_path, 0.0};

    auto core = std::make_unique<nativecore::GBCore>();
    if (!core->loadROM(rom_data))
      return {nativecore::testing::TestResult::Error,
              "Failed to parse ROM: " + rom_path, 0.0};

    return nativecore::testing::runRegisterPatternTest(
        *core, *core, mooneyePassPattern(), mooneyeFailPattern(),
        timeout_seconds);
  };
}

} // namespace gb_tests
