#pragma once

#include "nativecore/core.h"
#include "nativecore/testable_core.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nativecore::testing {

enum class TestResult { Pass, Fail, Timeout, Error };

struct TestOutcome {
  TestResult result;
  std::string message;
  double elapsed_ms;
};

void printResult(const std::string &name, const TestOutcome &outcome);

std::vector<uint8_t> loadFile(const std::string &path);

TestOutcome runSerialOutputTest(Core &core, TestableCore &testable,
                                int timeout_seconds = 120);

struct RegisterPattern {
  std::vector<std::pair<int, uint8_t>> regs; // {register_id, expected_value}
};

TestOutcome runRegisterPatternTest(Core &core, TestableCore &testable,
                                   const RegisterPattern &pass_pattern,
                                   const RegisterPattern &fail_pattern,
                                   int timeout_seconds = 30);

void runTestDirectory(
    const std::string &dir, const std::string &suite_name,
    const std::string &rom_extension, const std::vector<std::string> &blacklist,
    const std::function<TestOutcome(const std::string &)> &runner_fn,
    int &total, int &passed, int &failed, int &errors);

// Prints a summary line and returns 0 on all passing, 1 on failure of any.
int printSummary(int total, int passed, int failed, int errors);

} // namespace nativecore::testing
