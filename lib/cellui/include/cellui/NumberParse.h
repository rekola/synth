#pragma once

#include <system_error>

namespace cellui {

// What std::from_chars returns: where parsing stopped and how it went.
struct ParseResult {
  const char * ptr;
  std::errc ec;
};

// std::from_chars for floating point, on every platform: no leading
// whitespace or '+' is skipped, "inf" and "nan" parse, the locale is never
// consulted, `ptr` is the first character not used, and `ec` is
// std::errc() on success, invalid_argument when no number was parsed (`ptr`
// is `first`, `out` is untouched) and result_out_of_range when the value
// does not fit (`out` is untouched). The standard library is used where it has
// the function; otherwise fast_float (libc++ gates it on the deployment
// target, macOS 26.0).
ParseResult parseFloat(const char * first, const char * last, double & out);
ParseResult parseFloat(const char * first, const char * last, float & out);

namespace detail {

// The two implementations behind parseFloat(), for the tests, which run the
// same cases through both. parseFloatStd() is only usable when
// hasStdFloatFromChars() says the build has the standard one.
bool hasStdFloatFromChars();
ParseResult parseFloatStd(const char * first, const char * last, double & out);
ParseResult parseFloatStd(const char * first, const char * last, float & out);
ParseResult parseFloatFastFloat(const char * first, const char * last, double & out);
ParseResult parseFloatFastFloat(const char * first, const char * last, float & out);

}  // namespace detail

}  // namespace cellui
