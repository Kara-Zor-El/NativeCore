#include "gb_harness.h"

int main() {
  std::string base = gb_tests::findTestRomDir();
  int total = 0, passed = 0, failed = 0, errors = 0;
  std::printf("NativeCore GB Test: mooneye/acceptance\n\n");
  nativecore::testing::runTestDirectory(
      base + "/mooneye-test-suite/acceptance", "mooneye/acceptance", ".gb",
      gb_tests::mooneyeBlacklist(), gb_tests::makeMooneyeRunner(), total,
      passed, failed, errors);
  return nativecore::testing::printSummary(total, passed, failed, errors);
}
