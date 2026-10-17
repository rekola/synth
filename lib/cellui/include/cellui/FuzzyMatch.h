#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace cellui {

// How well a query matches a name, and which characters of the name it
// matched (for highlighting).
struct FuzzyMatch {
  int score = 0;
  std::vector<int> positions;
};

// Matches `query` against `text` as a case-insensitive subsequence (the
// query's characters in order, not necessarily adjacent; spaces in the query
// are ignored). Higher scores are better: an exact match beats a prefix,
// which beats characters at word starts (after '-', ' ' or '_'), which beat
// runs of adjacent characters, which beat scattered ones; shorter names win
// ties. An empty query matches everything with score 0.
std::optional<FuzzyMatch> fuzzyMatch(std::string_view query, std::string_view text);

}  // namespace cellui
