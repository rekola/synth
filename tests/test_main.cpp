#include "TestFramework.h"

int g_test_failures = 0;
int g_test_checks = 0;

int main() {
  int failed_tests = 0;

  for (auto & tc : testRegistry()) {
    int before = g_test_failures;
    std::printf("--- %s ---\n", tc.name.c_str());
    tc.fn();
    bool ok = g_test_failures == before;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", tc.name.c_str());
    if (!ok) failed_tests++;
  }

  std::printf("\n%d checks, %d/%zu tests failed\n", g_test_checks, failed_tests, testRegistry().size());
  return failed_tests == 0 ? 0 : 1;
}
