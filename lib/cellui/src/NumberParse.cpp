#include <cellui/NumberParse.h>

#include <charconv>

#include <fast_float/fast_float.h>

// Whether this standard library has std::from_chars for floating point. libc++
// declares it from LLVM 20 but marks it unavailable below the deployment
// target that ships it (a macOS dylib); it never defines __cpp_lib_to_chars,
// and __builtin_available cannot see an overload that is unavailable and not
// weakly linked, so its own availability macro is the test. Other libraries
// define __cpp_lib_to_chars when they have it.
#if defined(_LIBCPP_VERSION)
#if defined(_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT) && _LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT
#define CELLUI_STD_FLOAT_FROM_CHARS 1
#endif
#elif defined(__cpp_lib_to_chars)
#define CELLUI_STD_FLOAT_FROM_CHARS 1
#endif

namespace cellui {

namespace {

template <typename T>
ParseResult fastFloat(const char * first, const char * last, T & out) {
  T value = out;
  fast_float::from_chars_result r = fast_float::from_chars(first, last, value, fast_float::chars_format::general);
  // Like the standard function, leave `out` alone unless a number was read.
  if (r.ec == std::errc()) out = value;
  return {r.ptr, r.ec};
}

}  // namespace

namespace detail {

bool hasStdFloatFromChars() {
#ifdef CELLUI_STD_FLOAT_FROM_CHARS
  return true;
#else
  return false;
#endif
}

#ifdef CELLUI_STD_FLOAT_FROM_CHARS
namespace {
template <typename T>
ParseResult standard(const char * first, const char * last, T & out) {
  // Some libraries store inf or 0 when the value is out of range.
  T value = out;
  std::from_chars_result r = std::from_chars(first, last, value, std::chars_format::general);
  if (r.ec == std::errc()) out = value;
  return {r.ptr, r.ec};
}
}  // namespace
ParseResult parseFloatStd(const char * first, const char * last, double & out) { return standard(first, last, out); }
ParseResult parseFloatStd(const char * first, const char * last, float & out) { return standard(first, last, out); }
#else
ParseResult parseFloatStd(const char * first, const char *, double &) { return {first, std::errc::function_not_supported}; }
ParseResult parseFloatStd(const char * first, const char *, float &) { return {first, std::errc::function_not_supported}; }
#endif

ParseResult parseFloatFastFloat(const char * first, const char * last, double & out) { return fastFloat(first, last, out); }
ParseResult parseFloatFastFloat(const char * first, const char * last, float & out) { return fastFloat(first, last, out); }

}  // namespace detail

#ifdef CELLUI_STD_FLOAT_FROM_CHARS
ParseResult parseFloat(const char * first, const char * last, double & out) { return detail::parseFloatStd(first, last, out); }
ParseResult parseFloat(const char * first, const char * last, float & out) { return detail::parseFloatStd(first, last, out); }
#else
ParseResult parseFloat(const char * first, const char * last, double & out) { return detail::parseFloatFastFloat(first, last, out); }
ParseResult parseFloat(const char * first, const char * last, float & out) { return detail::parseFloatFastFloat(first, last, out); }
#endif

}  // namespace cellui
