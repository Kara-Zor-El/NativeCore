#include "gb_core.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::vector<uint8_t> loadFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open())
    return {};
  auto size = file.tellg();
  file.seekg(0);
  std::vector<uint8_t> data(size);
  file.read(reinterpret_cast<char *>(data.data()), size);
  return data;
}

enum class TestResult {
  Pass,
  Fail,
  Timeout,
  Error,
};

struct TestOutcome {
  TestResult result;
  std::string message;
  double elapsed_ms;
};

static TestOutcome runBlarggTest(const std::string &rom_path,
                                 int timeout_seconds = 120) {
  auto rom_data = loadFile(rom_path);
  if (rom_data.empty()) {
    return {TestResult::Error, "Failed to load ROM: " + rom_path, 0};
  }

  auto core = std::make_unique<nativecore::GBCore>();
  if (!core->loadROM(rom_data)) {
    return {TestResult::Error, "Failed to parse ROM: " + rom_path, 0};
  }

  auto start = std::chrono::steady_clock::now();
  double fps = core->systemInfo().native_fps;
  int max_frames = static_cast<int>(timeout_seconds * fps);

  for (int frame = 0; frame < max_frames; frame++) {
    core->tick();

    const auto &serial = core->serialOutput();
    if (serial.find("Passed") != std::string::npos) {
      auto end = std::chrono::steady_clock::now();
      double ms =
          std::chrono::duration<double, std::milli>(end - start).count();
      return {TestResult::Pass, serial, ms};
    }
    if (serial.find("Failed") != std::string::npos) {
      auto end = std::chrono::steady_clock::now();
      double ms =
          std::chrono::duration<double, std::milli>(end - start).count();
      return {TestResult::Fail, serial, ms};
    }
  }

  auto end = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  return {TestResult::Timeout,
          "Timeout after " + std::to_string(timeout_seconds) + "s.", ms};
}

static TestOutcome runMooneyeTest(const std::string &rom_path,
                                  int timeout_seconds = 30) {
  auto rom_data = loadFile(rom_path);
  if (rom_data.empty()) {
    return {TestResult::Error, "Failed to load ROM: " + rom_path, 0};
  }

  auto core = std::make_unique<nativecore::GBCore>();
  if (!core->loadROM(rom_data)) {
    return {TestResult::Error, "Failed to parse ROM: " + rom_path, 0};
  }

  auto start = std::chrono::steady_clock::now();
  double fps = core->systemInfo().native_fps;
  int max_frames = static_cast<int>(timeout_seconds * fps);

  for (int frame = 0; frame < max_frames; frame++) {
    core->tick();

    // Mooneye tests use: LD B,B; LD D,D; LD H,H (opcodes 40 58 64) for pass
    // and: LD B,B; LD E,E; LD L,L (opcodes 40 5B 6D) for fail
    // We detect via the Fibonacci result in registers:
    // Pass: B=3, C=5, D=8, E=13, H=21, L=34
    auto &cpu = core->cpu();
    if (cpu.b() == 3 && cpu.c() == 5 && cpu.d() == 8 && cpu.e() == 13 &&
        cpu.h() == 21 && cpu.l() == 34) {
      auto end = std::chrono::steady_clock::now();
      double ms =
          std::chrono::duration<double, std::milli>(end - start).count();
      return {TestResult::Pass, "Fibonacci sequence in registers (pass)", ms};
    }

    // for failure it:
    // writes the byte 0x42 to the registers B/C/D/E/H/L
    // executes an LD B, B opcode
    // sends the byte 0x42 6 times using the serial port
    // executes an LD B, B opcode, followed by an infinite JR loop (JR pointing
    // to itself) for this, we will just check the registers
    if (cpu.b() == 0x42 && cpu.c() == 0x42 && cpu.d() == 0x42 &&
        cpu.e() == 0x42 && cpu.h() == 0x42 && cpu.l() == 0x42) {
      auto end = std::chrono::steady_clock::now();
      double ms =
          std::chrono::duration<double, std::milli>(end - start).count();
      return {TestResult::Fail, "", ms};
    }
  }

  auto end = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  return {TestResult::Timeout, "Timeout", ms};
}

static const char *resultStr(TestResult r) {
  switch (r) {
  case TestResult::Pass:
    return "PASS";
  case TestResult::Fail:
    return "FAIL";
  case TestResult::Timeout:
    return "TIMEOUT";
  case TestResult::Error:
    return "ERROR";
  }
  return "UNKNOWN";
}

static void printResult(const std::string &name, const TestOutcome &outcome) {
  const char *color;
  switch (outcome.result) {
  case TestResult::Pass:
    color = "\033[32m";
    break; // green
  case TestResult::Fail:
    color = "\033[31m";
    break; // red
  case TestResult::Timeout:
    color = "\033[33m";
    break; // yellow
  case TestResult::Error:
    color = "\033[35m";
    break; // magenta
  }
  std::printf("%s[%s]\033[0m %-50s (%.1f ms)\n", color,
              resultStr(outcome.result), name.c_str(), outcome.elapsed_ms);
  if (outcome.result != TestResult::Pass && !outcome.message.empty()) {
    // Truncate long messages
    auto msg = outcome.message.substr(0, 200);
    // Replace newlines for compact display
    for (auto &c : msg) {
      if (c == '\n')
        c = ' ';
    }
    std::printf("         %s\n", msg.c_str());
  }
}

static void runTestDirectory(const std::string &dir, const std::string &suite,
                             int &total, int &passed, int &failed,
                             int &errors) {
  if (!fs::exists(dir)) {
    std::printf("Test directory not found: %s\n", dir.c_str());
    return;
  }

  // NOTE: Not all roms within the mooneye test suite are made for this CPU
  // revision
  std::vector<std::string> blacklisted_roms = {
      "boot_div-S.gb",    "boot_div-dmg0.gb",  "boot_div2-S.gb",
      "boot_hwio-S.gb",   "boot_hwio-dmg0.gb", "boot_regs-dmg0.gb",
      "boot_regs-mgb.gb", "boot_regs-sgb.gb",  "boot_regs-sgb2.gb"};

  std::vector<std::string> rom_files;
  for (auto &entry : fs::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file()) {
      auto ext = entry.path().extension().string();
      if (ext == ".gb") {
        rom_files.push_back(entry.path().string());
      }
    }
  }
  std::sort(rom_files.begin(), rom_files.end());

  for (auto &rom_path : rom_files) {
    auto rel_path = fs::relative(rom_path, dir).string();

    bool blacklisted =
        std::any_of(blacklisted_roms.begin(), blacklisted_roms.end(),
                    [&rel_path](const std::string &b) {
                      return rel_path.find(b) != std::string::npos;
                    });

    if (blacklisted)
      continue;

    auto test_name = suite + "/" + rel_path;
    total++;

    TestOutcome outcome;
    if (suite.find("mooneye") != std::string::npos) {
      outcome = runMooneyeTest(rom_path);
    } else {
      outcome = runBlarggTest(rom_path);
    }

    printResult(test_name, outcome);
    switch (outcome.result) {
    case TestResult::Pass:
      passed++;
      break;
    case TestResult::Fail:
      failed++;
      break;
    default:
      errors++;
      break;
    }
  }
}

int main(int argc, char *argv[]) {
  std::string test_dir;
  std::string filter;

  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
      test_dir = argv[++i];
    } else if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
      filter = argv[++i];
    } else if (std::strcmp(argv[i], "--help") == 0) {
      std::printf("Usage: gb_test_runner [options]\n"
                  "  --dir <path>     Path to extracted test ROM directory\n"
                  "  --filter <name>  Only run tests matching this substring\n"
                  "  --help           Show this help\n"
                  "\nExpected directory structure:\n"
                  "  <dir>/blargg/cpu_instrs/\n"
                  "  <dir>/blargg/instr_timing/\n"
                  "  <dir>/blargg/mem_timing/\n"
                  "  <dir>/mooneye-test-suite/\n"
                  "\nDownload test ROMs from:\n"
                  "  https://github.com/c-sp/game-boy-test-roms/releases\n");
      return 0;
    } else {
      // Treat bare argument as single ROM file
      std::printf("Running single test: %s\n", argv[i]);
      auto outcome = runBlarggTest(argv[i]);
      printResult(argv[i], outcome);
      return outcome.result == TestResult::Pass ? 0 : 1;
    }
  }

  if (test_dir.empty()) {
    std::fprintf(stderr,
                 "No test ROM directory found. Use --dir <path> or place "
                 "test ROMs in ./test-roms/\n"
                 "Download from: "
                 "https://github.com/c-sp/game-boy-test-roms/releases\n");
    return 1;
  }

  std::printf("NativeCore Game Boy Test Runner\n");
  std::printf("Test ROM directory: %s\n\n", test_dir.c_str());

  int total = 0, passed = 0, failed = 0, errors = 0;

  struct TestSuite {
    const char *dir_suffix;
    const char *name;
  };

  TestSuite suites[] = {
      {"blargg/cpu_instrs", "blargg/cpu_instrs"},
      {"blargg/instr_timing", "blargg/instr_timing"},
      {"blargg/mem_timing", "blargg/mem_timing"},
      {"blargg/mem_timing-2", "blargg/mem_timing-2"},
      {"blargg/halt_bug", "blargg/halt_bug"},
      {"mooneye-test-suite/acceptance", "mooneye/acceptance"},
      {"mooneye-test-suite/emulator-only", "mooneye/emulator-only"},
  };

  for (auto &suite : suites) {
    std::string suite_dir = test_dir + "/" + suite.dir_suffix;
    if (!fs::exists(suite_dir))
      continue;
    if (!filter.empty() &&
        std::string(suite.name).find(filter) == std::string::npos)
      continue;

    std::printf("\n--- %s ---\n", suite.name);
    runTestDirectory(suite_dir, suite.name, total, passed, failed, errors);
  }

  std::printf("\nResults\n");
  std::printf("Total: %d | ", total);
  std::printf("\033[32mPassed: %d\033[0m | ", passed);
  std::printf("\033[31mFailed: %d\033[0m | ", failed);
  std::printf("\033[33mErrors/Timeouts: %d\033[0m | ", errors);

  if (total > 0) {
    std::printf("Pass rate: %.1f%%\n", 100.0 * passed / total);
  }

  return (failed > 0 || errors > 0) ? 1 : 0;
}
