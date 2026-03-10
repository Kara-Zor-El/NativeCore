#include "test_runner.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace nativecore::testing {

std::vector<uint8_t> loadFile(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open())
    return {};
  auto size = file.tellg();
  file.seekg(0);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  file.read(reinterpret_cast<char *>(data.data()), size);
  return data;
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

void printResult(const std::string &name, const TestOutcome &outcome) {
  const char *color;
  switch (outcome.result) {
  case TestResult::Pass:
    color = "\033[32m";
    break;
  case TestResult::Fail:
    color = "\033[31m";
    break;
  case TestResult::Timeout:
    color = "\033[33m";
    break;
  case TestResult::Error:
    color = "\033[35m";
    break;
  }
  std::printf("%s[%s]\033[0m %-50s (%.1f ms)\n", color,
              resultStr(outcome.result), name.c_str(), outcome.elapsed_ms);
  if (outcome.result != TestResult::Pass && !outcome.message.empty()) {
    auto msg = outcome.message.substr(0, 200);
    for (auto &c : msg) {
      if (c == '\n')
        c = ' ';
    }
    std::printf("         %s\n", msg.c_str());
  }
}

// Reads the null-terminated text output from cart RAM at $A004 (Blargg
// protocol).
static std::string readCartTextOut(Core &core) {
  std::string out;
  for (uint16_t addr = 0xA004; addr < 0xB000; addr++) {
    uint8_t ch = core.readMemory(addr);
    if (ch == 0)
      break;
    out += static_cast<char>(ch);
  }
  return out;
}

TestOutcome runSerialOutputTest(Core &core, TestableCore &testable,
                                int timeout_seconds) {
  auto start = std::chrono::steady_clock::now();
  double fps = core.systemInfo().native_fps;
  int max_frames = static_cast<int>(timeout_seconds * fps);

  for (int frame = 0; frame < max_frames; frame++) {
    core.tick();

    const auto &serial = testable.debugOutput();
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

    // Blargg cart-RAM result protocol:
    // $A001–$A003 = DE B0 61 (marker), $A000 = result (0x80 = not done,
    // 0 = pass, else fail)
    if (core.readMemory(0xA001) == 0xDE && core.readMemory(0xA002) == 0xB0 &&
        core.readMemory(0xA003) == 0x61) {
      uint8_t final_result = core.readMemory(0xA000);
      if (final_result != 0x80) {
        auto text = readCartTextOut(core);
        auto end = std::chrono::steady_clock::now();
        double ms =
            std::chrono::duration<double, std::milli>(end - start).count();
        if (final_result == 0) {
          return {TestResult::Pass, text, ms};
        } else {
          return {TestResult::Fail, text, ms};
        }
      }
    }
  }

  std::string timeout_msg =
      "Timeout after " + std::to_string(timeout_seconds) + "s.";
  const auto &serial = testable.debugOutput();
  if (!serial.empty())
    timeout_msg += " Serial: " + serial;
  auto text = readCartTextOut(core);
  if (!text.empty())
    timeout_msg += " Cart output: " + text;

  auto end = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  return {TestResult::Timeout, timeout_msg, ms};
}

static bool matchesPattern(Core &core, TestableCore &testable,
                           const RegisterPattern &pattern) {
  for (const auto &[id, expected] : pattern.regs) {
    if (testable.peekRegister(id) != expected)
      return false;
  }
  return !pattern.regs.empty();
}

TestOutcome runRegisterPatternTest(Core &core, TestableCore &testable,
                                   const RegisterPattern &pass_pattern,
                                   const RegisterPattern &fail_pattern,
                                   int timeout_seconds) {
  auto start = std::chrono::steady_clock::now();
  double fps = core.systemInfo().native_fps;
  int max_frames = static_cast<int>(timeout_seconds * fps);

  for (int frame = 0; frame < max_frames; frame++) {
    core.tick();

    if (matchesPattern(core, testable, pass_pattern)) {
      auto end = std::chrono::steady_clock::now();
      double ms =
          std::chrono::duration<double, std::milli>(end - start).count();
      return {TestResult::Pass, "Register pass pattern matched", ms};
    }
    if (matchesPattern(core, testable, fail_pattern)) {
      auto end = std::chrono::steady_clock::now();
      double ms =
          std::chrono::duration<double, std::milli>(end - start).count();
      return {TestResult::Fail, "Register fail pattern matched", ms};
    }
  }

  auto end = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(end - start).count();
  return {TestResult::Timeout, "Timeout", ms};
}

void runTestDirectory(
    const std::string &dir, const std::string &suite_name,
    const std::string &rom_extension, const std::vector<std::string> &blacklist,
    const std::function<TestOutcome(const std::string &)> &runner_fn,
    int &total, int &passed, int &failed, int &errors) {
  if (!fs::exists(dir)) {
    std::printf("Test directory not found: %s\n", dir.c_str());
    return;
  }

  std::vector<std::string> rom_files;
  for (const auto &entry : fs::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file() &&
        entry.path().extension().string() == rom_extension) {
      rom_files.push_back(entry.path().string());
    }
  }
  std::sort(rom_files.begin(), rom_files.end());

  for (const auto &rom_path : rom_files) {
    auto rel_path = fs::relative(rom_path, dir).string();

    bool blacklisted = std::any_of(
        blacklist.begin(), blacklist.end(), [&rel_path](const std::string &b) {
          return rel_path.find(b) != std::string::npos;
        });
    if (blacklisted)
      continue;

    auto test_name = suite_name + "/" + rel_path;
    total++;

    auto outcome = runner_fn(rom_path);
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

int printSummary(int total, int passed, int failed, int errors) {
  std::printf("\nResults\n");
  std::printf("Total: %d | ", total);
  std::printf("\033[32mPassed: %d\033[0m | ", passed);
  std::printf("\033[31mFailed: %d\033[0m | ", failed);
  std::printf("\033[33mErrors/Timeouts: %d\033[0m", errors);
  if (total > 0)
    std::printf(" | Pass rate: %.1f%%", 100.0 * passed / total);
  std::printf("\n");
  return (failed > 0 || errors > 0) ? 1 : 0;
}

} // namespace nativecore::testing
