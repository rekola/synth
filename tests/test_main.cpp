#include "TestFramework.h"

#include <cstdlib>

int g_test_failures = 0;
int g_test_checks = 0;

int main() {
  int failed_tests = 0;

  for (auto & tc : testRegistry()) {
    int before = g_test_failures;
    std::printf("--- %s ---\n", tc.name.c_str());
    // Reseed before every test - rand() (e.g. NoteMultiplier's detune/
    // phase spread, SongState's velocity/delay randomization) is otherwise
    // one continuous, shared sequence across the whole binary in
    // registration order, so a test's outcome would silently depend on
    // how many rand() calls every earlier-run test happened to make -
    // adding or reordering an unrelated test could then flip a later
    // test's random outcome without changing anything the later test
    // actually verifies.
    srand(1);
    tc.fn();
    bool ok = g_test_failures == before;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", tc.name.c_str());
    if (!ok) failed_tests++;
  }

  std::printf("\n%d checks, %d/%zu tests failed\n", g_test_checks, failed_tests, testRegistry().size());
  return failed_tests == 0 ? 0 : 1;
}
