#include "gb_harness.h"

int main() {
  std::string base = gb_tests::findTestRomDir();
  int total = 0, passed = 0, failed = 0, errors = 0;
  std::printf("NativeCore GB Test: blargg/instr_timing\n\n");
  nativecore::testing::runTestDirectory(
      base + "/blargg/instr_timing", "blargg/instr_timing", ".gb", {},
      gb_tests::makeBlarggRunner(), total, passed, failed, errors);
  return nativecore::testing::printSummary(total, passed, failed, errors);
}
