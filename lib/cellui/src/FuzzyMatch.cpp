#include <cellui/FuzzyMatch.h>

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace cellui {

namespace {

constexpr int kMatchBonus = 16;
constexpr int kAdjacentBonus = 24;
constexpr int kWordStartBonus = 32;
constexpr int kPrefixBonus = 48;
constexpr int kGapPenalty = 3;
constexpr int kLeadingPenalty = 2;
constexpr int kExactBonus = 1000;

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool startsWord(std::string_view text, size_t i) {
  if (i == 0) return true;
  char prev = text[i - 1];
  return prev == '-' || prev == ' ' || prev == '_';
}

}  // namespace

std::optional<FuzzyMatch> fuzzyMatch(std::string_view query, std::string_view text) {
  std::vector<char> q;
  for (char c : query) {
    if (c != ' ') q.push_back(lower(c));
  }
  FuzzyMatch result;
  if (q.empty()) return result;
  const size_t n = text.size(), m = q.size();
  if (m > n) return std::nullopt;

  // best[j][i]: the best score matching the first j query characters with
  // the j-th one on text[i]; -1 when there is no such match.
  constexpr int kNone = -1000000;
  std::vector<std::vector<int>> best(m, std::vector<int>(n, kNone));
  std::vector<std::vector<int>> from(m, std::vector<int>(n, -1));
  for (size_t j = 0; j < m; j++) {
    for (size_t i = j; i < n; i++) {
      if (lower(text[i]) != q[j]) continue;
      int here = kMatchBonus + (startsWord(text, i) ? kWordStartBonus : 0);
      if (j == 0) {
        best[j][i] = here - kLeadingPenalty * static_cast<int>(i) + (i == 0 ? kPrefixBonus : 0);
        continue;
      }
      for (size_t k = j - 1; k < i; k++) {
        if (best[j - 1][k] == kNone) continue;
        int gap = static_cast<int>(i - k - 1);
        int score = best[j - 1][k] + here + (gap == 0 ? kAdjacentBonus : -kGapPenalty * gap);
        if (score > best[j][i]) {
          best[j][i] = score;
          from[j][i] = static_cast<int>(k);
        }
      }
    }
  }
  int best_end = -1, best_score = kNone;
  for (size_t i = 0; i < n; i++) {
    if (best[m - 1][i] > best_score) {
      best_score = best[m - 1][i];
      best_end = static_cast<int>(i);
    }
  }
  if (best_end < 0) return std::nullopt;

  result.positions.assign(m, 0);
  int at = best_end;
  for (size_t j = m; j-- > 0;) {
    result.positions[j] = at;
    at = from[j][static_cast<size_t>(at)];
  }
  // Shorter names win ties, and the whole name matching is best of all.
  result.score = best_score - static_cast<int>(n - m);
  if (n == m && result.positions.back() == static_cast<int>(n) - 1 && result.positions.front() == 0) {
    bool exact = true;
    for (size_t i = 0; i < n; i++) exact = exact && lower(text[i]) == q[i];
    if (exact) result.score += kExactBonus;
  }
  return result;
}

}  // namespace cellui
