#ifndef _TUNINGTESTSUPPORT_H_
#define _TUNINGTESTSUPPORT_H_

#include <cmath>
#include <optional>

// A gamut correction rounded to whole cents: for tests that check which ratio
// a key got, not how finely it is written.
inline std::optional<int> wholeCents(std::optional<double> cents) {
  return cents ? std::optional<int>(static_cast<int>(std::lround(*cents))) : std::nullopt;
}

#endif
