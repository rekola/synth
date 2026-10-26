#ifndef _RATIO_H_
#define _RATIO_H_

#include <cmath>
#include <compare>
#include <cstdint>
#include <numeric>

// A just-intonation frequency ratio num/den, always in lowest terms with
// num, den > 0. Equality and ordering compare the value; 2/1 != 1/1, so
// octave-reduce first where pitch class is what matters.
struct Ratio {
  int num = 1;
  int den = 1;

  constexpr Ratio() = default;

  constexpr Ratio(std::int64_t n, std::int64_t d) {
    if (n <= 0 || d <= 0) badRatio();
    const std::int64_t g = std::gcd(n, d);
    n /= g;
    d /= g;
    if (n > INT32_MAX || d > INT32_MAX) badRatio();
    num = static_cast<int>(n);
    den = static_cast<int>(d);
  }

  // Octave-reduced into [1, 2).
  constexpr Ratio reduced() const {
    std::int64_t n = num, d = den;
    while (n >= 2 * d) d *= 2;
    while (n < d) n *= 2;
    return Ratio{n, d};
  }

  // The utonal mirror 1/r, octave-reduced; 7/6 -> 12/7, 1/1 -> 1/1.
  constexpr Ratio mirror() const { return Ratio{den, num}.reduced(); }

  constexpr bool isOctaveOf(Ratio o) const { return reduced() == o.reduced(); }

  double cents() const { return 1200.0 * std::log2(static_cast<double>(num) / den); }

  // Nearest step of an equal division of the octave.
  int steps(int edo) const { return static_cast<int>(std::lround(cents() * edo / 1200.0)); }

  friend constexpr Ratio operator*(Ratio a, Ratio b) {
    return Ratio{std::int64_t{a.num} * b.num, std::int64_t{a.den} * b.den};
  }
  friend constexpr Ratio operator/(Ratio a, Ratio b) {
    return Ratio{std::int64_t{a.num} * b.den, std::int64_t{a.den} * b.num};
  }
  friend constexpr Ratio operator*(Ratio a, int n) { return Ratio{std::int64_t{a.num} * n, a.den}; }
  friend constexpr Ratio operator*(int n, Ratio a) { return a * n; }
  friend constexpr Ratio operator/(Ratio a, int n) { return Ratio{a.num, std::int64_t{a.den} * n}; }
  friend constexpr Ratio operator/(int n, Ratio a) { return Ratio{std::int64_t{n} * a.den, a.num}; }

  friend constexpr bool operator==(Ratio, Ratio) = default;
  friend constexpr std::strong_ordering operator<=>(Ratio a, Ratio b) {
    return std::int64_t{a.num} * b.den <=> std::int64_t{b.num} * a.den;
  }

 private:
  // Declared, never defined: reaching it in constant evaluation is a
  // compile error naming this function; reaching it at run time is a
  // link error, which these constructors should never produce.
  static void badRatio();
};

namespace ratio_literals {
// 7/6_r is 7 / Ratio{6, 1} == Ratio{7, 6}. Without the suffix, 7/6 is
// integer division; static_assert your scale tables.
consteval Ratio operator""_r(unsigned long long n) {
  return Ratio{static_cast<std::int64_t>(n), 1};
}
} // namespace ratio_literals

#endif
