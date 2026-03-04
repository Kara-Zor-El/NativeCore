#include "gb_harness.h"

int main() {
  std::string base = gb_tests::findTestRomDir();
  int total = 0, passed = 0, failed = 0, errors = 0;
  std::printf("NativeCore GB Test: blargg/halt_bug\n\n");
  nativecore::testing::runTestDirectory(
      base + "/blargg/halt_bug", "blargg/halt_bug", ".gb", {},
      gb_tests::makeBlarggRunner(), total, passed, failed, errors);
  return nativecore::testing::printSummary(total, passed, failed, errors);
}
