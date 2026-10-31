#include "TestFramework.h"

#include <cellui/NumberParse.h>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace cellui;

namespace {

struct Case {
  const char * text;
  std::errc ec;
  size_t used;       // characters up to ptr
  double value;      // on success
  bool negative_zero = false;
};

const double kNan = std::nan("");
const double kInf = HUGE_VAL;

// The cases std::from_chars is specified by; "used" is where ptr ends up.
const std::vector<Case> & doubleCases() {
  static const std::vector<Case> cases = {
    {"3.14", std::errc(), 4, 3.14},
    {"1e308", std::errc(), 5, 1e308},
    {"1e309", std::errc::result_out_of_range, 5, 0},
    {"1e-400", std::errc::result_out_of_range, 6, 0},
    {".5", std::errc(), 2, 0.5},
    {"5.", std::errc(), 2, 5.0},
    {"-0", std::errc(), 2, 0.0, true},
    {"nan", std::errc(), 3, kNan},
    {"inf", std::errc(), 3, kInf},
    {"+1", std::errc::invalid_argument, 0, 0},
    {" 1", std::errc::invalid_argument, 0, 0},
    {"1abc", std::errc(), 1, 1.0},
    {"", std::errc::invalid_argument, 0, 0},
    {"1,5", std::errc(), 1, 1.0},
    {"-", std::errc::invalid_argument, 0, 0},
    {".", std::errc::invalid_argument, 0, 0},
    {"1e", std::errc(), 1, 1.0},
    {"60deg", std::errc(), 2, 60.0},
    {"2.5%", std::errc(), 3, 2.5},
    {"-1.5e2x", std::errc(), 6, -150.0},
  };
  return cases;
}

const std::vector<Case> & floatCases() {
  static const std::vector<Case> cases = {
    {"3.14", std::errc(), 4, 3.14f},
    {"3.4e38", std::errc(), 6, 3.4e38f},
    {"1e39", std::errc::result_out_of_range, 4, 0},
    {"1e-60", std::errc::result_out_of_range, 5, 0},
    {"+1", std::errc::invalid_argument, 0, 0},
    {" 1", std::errc::invalid_argument, 0, 0},
    {"0.7", std::errc(), 3, 0.7f},
  };
  return cases;
}

using Parse = std::function<ParseResult(const char *, const char *, double &)>;
using ParseF = std::function<ParseResult(const char *, const char *, float &)>;

template <typename T>
bool matches(const char * who, const Case & c, const std::function<ParseResult(const char *, const char *, T &)> & parse) {
  const T untouched = static_cast<T>(-12345.0);
  T value = untouched;
  const char * first = c.text;
  ParseResult r = parse(first, first + std::strlen(first), value);
  bool ok = r.ec == c.ec && r.ptr == first + c.used;
  if (c.ec == std::errc()) {
    if (std::isnan(c.value)) ok = ok && std::isnan(value);
    else if (std::isinf(c.value)) ok = ok && std::isinf(value) && value > 0;
    else ok = ok && value == static_cast<T>(c.value) && (c.value != 0.0 || std::signbit(value) == c.negative_zero);
  } else {
    ok = ok && value == untouched;  // a failed parse leaves it alone
  }
  if (!ok) std::fprintf(stderr, "  parseFloat [%s] \"%s\": ec=%d used=%d value=%g\n", who, c.text, static_cast<int>(r.ec), static_cast<int>(r.ptr - first), static_cast<double>(value));
  return ok;
}

bool all(const char * who, const Parse & parse, const ParseF & parse_float) {
  bool ok = true;
  for (const Case & c : doubleCases()) ok = matches<double>(who, c, parse) && ok;
  for (const Case & c : floatCases()) ok = matches<float>(who, c, parse_float) && ok;
  return ok;
}

Parse selectedDouble() {
  return [](const char * a, const char * b, double & v) { return parseFloat(a, b, v); };
}
ParseF selectedFloat() {
  return [](const char * a, const char * b, float & v) { return parseFloat(a, b, v); };
}
Parse fastDouble() {
  return [](const char * a, const char * b, double & v) { return detail::parseFloatFastFloat(a, b, v); };
}
ParseF fastFloatF() {
  return [](const char * a, const char * b, float & v) { return detail::parseFloatFastFloat(a, b, v); };
}
Parse stdDouble() {
  return [](const char * a, const char * b, double & v) { return detail::parseFloatStd(a, b, v); };
}
ParseF stdFloatF() {
  return [](const char * a, const char * b, float & v) { return detail::parseFloatStd(a, b, v); };
}

// A locale whose decimal separator is not a point, when the machine has one.
bool useCommaLocale(std::string & previous) {
  previous = std::setlocale(LC_ALL, nullptr);
  for (const char * name : {"de_DE.UTF-8", "fa_IR.UTF-8", "fi_FI.UTF-8", "fr_FR.UTF-8", "sv_SE.UTF-8"}) {
    if (std::setlocale(LC_ALL, name) && std::localeconv()->decimal_point[0] != '.') return true;
  }
  std::setlocale(LC_ALL, previous.c_str());
  return false;
}

}  // namespace

TEST(parsefloat_follows_from_chars_on_the_selected_implementation) {
  CHECK(all("selected", selectedDouble(), selectedFloat()));
}

TEST(parsefloat_fast_float_follows_from_chars) {
  CHECK(all("fast_float", fastDouble(), fastFloatF()));
}

TEST(parsefloat_standard_library_follows_from_chars_where_it_has_it) {
  if (!detail::hasStdFloatFromChars()) return;  // libc++ below its availability target
  CHECK(all("std", stdDouble(), stdFloatF()));
}

TEST(parsefloat_ignores_the_locale) {
  std::string previous;
  bool comma = useCommaLocale(previous);
  bool ok = all("selected, locale", selectedDouble(), selectedFloat());
  ok = all("fast_float, locale", fastDouble(), fastFloatF()) && ok;
  if (detail::hasStdFloatFromChars()) ok = all("std, locale", stdDouble(), stdFloatF()) && ok;
  double v = 0;
  const char * text = "0.75";
  ParseResult r = parseFloat(text, text + 4, v);
  ok = ok && r.ec == std::errc() && v == 0.75;
  std::setlocale(LC_ALL, previous.c_str());
  CHECK(ok);
  if (!comma) std::fprintf(stderr, "  (no comma-decimal locale installed: the locale was the C one)\n");
}
