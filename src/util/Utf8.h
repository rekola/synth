#pragma once

#include <string>

// Grapheme-cluster-aware UTF-8 display-width measurement and truncation,
// backed by libunistring (unigbrk.h/uniwidth.h - the header itself is only
// included in Utf8.cpp, not leaked here). See docs/known_bugs.md's former
// "PatternEditor::renderHeading() truncates ... by raw byte offset" entry
// for why a plain byte-offset std::string::erase() is wrong: it can land
// mid-codepoint (corrupting a multi-byte character) or, even cut at a
// codepoint boundary, split a grapheme cluster (e.g. a base character plus
// a combining accent) that has to stay together.
//
// Known gap: libunistring's grapheme-break function is a plain pairwise
// uc_is_grapheme_break(a, b) with no state beyond the two adjacent
// codepoints, so it can't implement UAX #29 rules that need to look further
// than one pair - regional-indicator (flag emoji) pairing and the
// emoji-ZWJ-sequence lookback rule in particular. Truncation still never
// splits any single codepoint, but a flag emoji or a multi-emoji ZWJ
// sequence can end up truncated between its own codepoints rather than
// treated as one unit - see docs/known_bugs.md. Ordinary text (accented
// characters, combining marks, non-BMP characters like this codebase's own
// 𝄪/𝄫/♮ - see Note.h) is unaffected.
namespace Utf8 {

// Terminal-column display width of the whole (well-formed) UTF-8 string.
int displayWidth(const std::string & text);

// Right-truncates text at the last grapheme-cluster boundary whose
// cumulative display width is <= max_columns - never splits a codepoint
// or a cluster. max_columns <= 0 returns "".
std::string truncateToWidth(const std::string & text, int max_columns);

// Right-pads text with ASCII spaces until displayWidth(text) == width; a
// no-op if text is already that wide or wider. Never truncates - combine
// with truncateToWidth() first when a call site needs both.
std::string padToWidth(const std::string & text, int width);

}
