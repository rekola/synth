#include "TestFramework.h"

#include "../src/util/Utf8.h"

// Layout-invariant style: rather than asserting exact column-width numbers
// (which depend on Unicode data tables this test can't hand-verify), most
// cases here build a string as [ascii prefix][multi-codepoint cluster][ascii
// suffix] and check that truncateToWidth() never returns a byte length that
// lands inside the cluster - only at 0, at an ascii-prefix boundary, at the
// cluster's own end, or further into the ascii suffix. That's the actual
// guarantee this module exists for (see docs/known_bugs.md's former entry on
// PatternEditor::renderHeading()'s byte-offset erase() bug).

namespace {

bool
inRange(size_t n, size_t lo, size_t hi) {
  return n >= lo && n < hi;
}

}  // namespace

TEST(utf8_truncate_ascii_matches_plain_byte_truncation) {
  CHECK(Utf8::truncateToWidth("hello world", 5) == "hello");
  CHECK(Utf8::truncateToWidth("hello", 5) == "hello");
  CHECK(Utf8::truncateToWidth("hello", 100) == "hello");
  CHECK(Utf8::truncateToWidth("", 5) == "");
}

TEST(utf8_pad_ascii_matches_plain_byte_padding) {
  CHECK(Utf8::padToWidth("hi", 5) == "hi   ");
  CHECK(Utf8::padToWidth("hello", 5) == "hello");
  CHECK(Utf8::padToWidth("hello world", 5) == "hello world");  // never truncates
}

TEST(utf8_display_width_ascii_is_byte_length) {
  CHECK(Utf8::displayWidth("") == 0);
  CHECK(Utf8::displayWidth("hello") == 5);
}

TEST(utf8_truncate_max_columns_non_positive_returns_empty) {
  CHECK(Utf8::truncateToWidth("hello", 0) == "");
  CHECK(Utf8::truncateToWidth("hello", -1) == "");
  CHECK(Utf8::truncateToWidth(std::string(), 0) == "");
}

// U+1D1AA MUSICAL SYMBOL DOUBLE SHARP (this codebase's own 𝄪, see Note.h) -
// a 4-byte astral (non-BMP) codepoint, single-codepoint grapheme cluster.
// "ab" (2 bytes) + 𝄪 (4 bytes, offsets [2,6)) + "cd" (2 bytes) = 8 bytes.
TEST(utf8_truncate_never_splits_a_4byte_astral_codepoint) {
  const std::string text = "ab\U0001D1AAcd";
  CHECK(text.size() == 8);
  for (int max_columns = 0; max_columns <= 10; max_columns++) {
    auto result = Utf8::truncateToWidth(text, max_columns);
    CHECK(!inRange(result.size(), 3, 6));  // never stop 1-3 bytes into the 4-byte glyph
    CHECK(text.compare(0, result.size(), result) == 0);  // result is always a real prefix of text
  }
}

// "x" + ("e" U+0065 + COMBINING ACUTE ACCENT U+0301, 1+2=3 bytes, one
// grapheme cluster) + "y" (1 byte) = 5 bytes total.
TEST(utf8_truncate_never_splits_base_char_from_combining_accent) {
  const std::string text = "x" "é" "y";  // 'x'(1) + 'e'(1) + U+0301(2) + 'y'(1) = 5 bytes
  CHECK(text.size() == 5);
  // Cluster boundaries: after 'x' (1), after "é" (4), after 'y' (5).
  for (int max_columns = 0; max_columns <= 6; max_columns++) {
    auto result = Utf8::truncateToWidth(text, max_columns);
    CHECK(result.size() != 2 && result.size() != 3);  // never a base char without its accent, or a lone accent byte
    CHECK(text.compare(0, result.size(), result) == 0);
  }
}

// Two REGIONAL INDICATOR SYMBOL codepoints (U+1F1EB U+1F1EE, "FI" -> the
// Finland flag) are supposed to merge into a single grapheme cluster under
// UAX #29's regional-indicator pairing rule (GB12/13) - but that rule needs
// to count how many regional indicators appeared in an unbroken run, which
// libunistring's pairwise uc_is_grapheme_break()-based u8_grapheme_next()
// has no state for (verified directly against the library: it reports a
// boundary between the two codepoints, unlike utf8proc's *stateful*
// grapheme-break function, which does merge them). Each codepoint still
// stays intact as its own cluster/codepoint though - this only asserts
// that narrower, still-true guarantee, not full UAX #29 compliance.
// "ab"(2) + flag(8, two 4-byte codepoints) + "cd"(2) = 12 bytes.
TEST(utf8_truncate_never_splits_a_single_regional_indicator_codepoint) {
  const std::string text = "ab" "\U0001F1EB\U0001F1EE" "cd";
  CHECK(text.size() == 12);
  for (int max_columns = 0; max_columns <= 14; max_columns++) {
    auto result = Utf8::truncateToWidth(text, max_columns);
    CHECK(!inRange(result.size(), 3, 6));   // never stop inside the first RI codepoint
    CHECK(!inRange(result.size(), 7, 10));  // never stop inside the second RI codepoint
    CHECK(text.compare(0, result.size(), result) == 0);
  }
}

// U+1F600 GRINNING FACE + ZERO WIDTH JOINER (U+200D) + U+1F600 again is
// supposed to form one grapheme cluster under UAX #29's emoji-ZWJ-sequence
// rule (GB11) - libunistring gets the *first* half right (ZWJ always
// attaches to the preceding character, its ordinary Extend rule, GB9) but
// doesn't implement GB11's lookback ("continue into the following character
// too, if the one before the ZWJ was Extended_Pictographic"), so the second
// emoji ends up in its own cluster (verified directly; utf8proc's stateful
// grapheme-break function merges the whole sequence into one cluster).
// "ab"(2) + emoji(4) + ZWJ(3) + emoji(4) + "cd"(2) = 15 bytes.
TEST(utf8_truncate_never_splits_the_emoji_plus_zwj_cluster_or_the_trailing_codepoint) {
  const std::string text = "ab" "\U0001F600\u200D\U0001F600" "cd";
  CHECK(text.size() == 15);
  for (int max_columns = 0; max_columns <= 17; max_columns++) {
    auto result = Utf8::truncateToWidth(text, max_columns);
    CHECK(!inRange(result.size(), 3, 9));    // never stop inside the emoji+ZWJ cluster
    CHECK(!inRange(result.size(), 10, 13));  // never stop inside the trailing emoji codepoint
    CHECK(text.compare(0, result.size(), result) == 0);
  }
}

TEST(utf8_pad_is_noop_when_already_at_or_above_target_width) {
  CHECK(Utf8::padToWidth("hello", 5) == "hello");
  CHECK(Utf8::padToWidth("hello", 3) == "hello");
}

TEST(utf8_pad_pads_by_display_columns_not_bytes) {
  // "é" is 3 bytes but 1 display column - the bug this module fixes
  // on the padding side: a byte-count pad loop would add too few spaces.
  auto padded = Utf8::padToWidth("é", 4);
  CHECK(Utf8::displayWidth(padded) == 4);
  CHECK(padded.size() == 3 + 3);  // 3 bytes of content + 3 ASCII space bytes
}

TEST(utf8_malformed_input_does_not_hang_and_stays_bounded) {
  // A truncated 2-byte lead ('\xC3' with nothing following) and a lone
  // continuation byte ('\x80') - not valid UTF-8, but must not spin the
  // truncation loop forever or read out of bounds.
  const std::string truncated_lead = "ab\xC3";
  const std::string lone_continuation = "ab\x80""cd";

  auto r1 = Utf8::truncateToWidth(truncated_lead, 10);
  CHECK(r1.size() <= truncated_lead.size());

  auto r2 = Utf8::truncateToWidth(lone_continuation, 10);
  CHECK(r2.size() <= lone_continuation.size());

  (void)Utf8::displayWidth(truncated_lead);
  (void)Utf8::displayWidth(lone_continuation);
}
