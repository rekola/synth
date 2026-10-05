#ifndef _TIMESIGNATURE_H_
#define _TIMESIGNATURE_H_

#include <cctype>
#include <cstdlib>
#include <optional>
#include <string>

// A row is a sixteenth note, so a signature needs a whole number of rows
// per beat: the denominator is 1, 2, 4, 8 or 16.
struct TimeSignature {
  int numerator = 0; // 0: none
  int denominator = 0;

  static bool validDenominator(int denominator) {
    return denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16;
  }
  static constexpr int kMaxNumerator = 32;

  bool isSet() const { return numerator > 0; }
  int rowsPerBar() const { return numerator * 16 / denominator; }
  int rowsPerBeat() const { return 16 / denominator; }
  bool operator==(const TimeSignature & other) const { return numerator == other.numerator && denominator == other.denominator; }
  bool operator!=(const TimeSignature & other) const { return !(*this == other); }

  std::string toString() const { return std::to_string(numerator) + "/" + std::to_string(denominator); }

  // "3/4" (spaces around the slash allowed); nullopt if it isn't one a row
  // grid can hold. A numerator of 0 is accepted and means none.
  static std::optional<TimeSignature> parse(const std::string & text) {
    auto slash = text.find('/');
    if (slash == std::string::npos) return std::nullopt;
    auto trim = [](std::string s) {
      auto first = s.find_first_not_of(' ');
      if (first == std::string::npos) return std::string();
      return s.substr(first, s.find_last_not_of(' ') - first + 1);
    };
    auto left = trim(text.substr(0, slash)), right = trim(text.substr(slash + 1));
    auto digits = [](const std::string & s) {
      if (s.empty() || s.size() > 2) return false;
      for (auto c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
      return true;
    };
    if (!digits(left) || !digits(right)) return std::nullopt;
    TimeSignature result{std::atoi(left.c_str()), std::atoi(right.c_str())};
    if (!validDenominator(result.denominator) || result.numerator > kMaxNumerator) return std::nullopt;
    return result;
  }

  // The signature a bar of `rows` rows reads as, for songs that only gave a
  // bar length: whole beats of 4 rows are quarter notes, anything else
  // counts sixteenths.
  static TimeSignature fromRowsPerBar(int rows) {
    if (rows <= 0) return {4, 4};
    if (rows % 4 == 0 && rows / 4 <= kMaxNumerator) return {rows / 4, 4};
    if (rows <= kMaxNumerator) return {rows, 16};
    return {4, 4};
  }
};

#endif
