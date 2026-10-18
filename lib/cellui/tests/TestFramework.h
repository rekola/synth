#ifndef _TESTFRAMEWORK_H_
#define _TESTFRAMEWORK_H_

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase> & testRegistry() {
  static std::vector<TestCase> registry;
  return registry;
}

struct TestRegistrar {
  TestRegistrar(const char * name, std::function<void()> fn) {
    testRegistry().push_back({name, std::move(fn)});
  }
};

// Registers a test function named `name` under TEST(name) { ... }. Each test
// is a free function collected into a global registry and run by
// tests/test_main.cpp; there is no per-test isolation beyond the process, so
// tests must not depend on global mutable state left by earlier tests.
#define TEST(name) \
  static void test_##name(); \
  static TestRegistrar registrar_##name(#name, test_##name); \
  static void test_##name()

extern int g_test_failures;
extern int g_test_checks;

#define CHECK(cond) \
  do { \
    g_test_checks++; \
    if (!(cond)) { \
      g_test_failures++; \
      std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
  } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) < (eps))

#endif
