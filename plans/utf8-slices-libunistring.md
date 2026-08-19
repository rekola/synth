# Grapheme-cluster-aware UTF-8 truncation/padding via libunistring

## Context

`docs/known_bugs.md` already documents the concrete bug this plan fixes:

> **`PatternEditor::renderHeading()` truncates track/instrument names by
> raw byte offset** (`std::string::erase(byte_pos)`), not by character -
> track names, instrument names, and SF2 preset names can all contain
> multi-byte UTF-8 (this codebase's own note names already use non-ASCII
> glyphs like 𝄪/𝄫/♮ elsewhere - see `Note.h`), and a byte-offset cut can
> land mid-sequence, corrupting the trailing character [...] Cutting by
> codepoint alone isn't actually correct either - a grapheme cluster (an
> accent/combining mark, a ZWJ emoji sequence, ...) can span several
> codepoints that must stay together - so the real fix needs
> grapheme-cluster-aware truncation [...] not a hand-rolled codepoint
> counter.

(That entry names `utf8proc` as the originally-envisioned library; this
plan uses `libunistring` instead - see "Why libunistring, not utf8proc"
below.)

Three call sites in `PatternEditor::renderHeading()` (`src/ui/PatternEditor.cpp`)
have this exact problem, each with a `// Byte-offset truncation, not
UTF-8-aware` caveat comment pointing at the known-bugs entry:

- `src/ui/PatternEditor.cpp:1570` - track name, `name.erase(text_width)`
- `src/ui/PatternEditor.cpp:1588` - instrument name, `instrument_name.erase(instrument_name_width)`
- `src/ui/PatternEditor.cpp:1596` - track element name, `name.erase(element_name_width)`

Each of these also right-pads with a byte-count `while (name.size() <
text_width) name += ' '` loop immediately below the truncation - which has
the same underlying bug in the opposite direction: for any string
containing a multi-byte character, `.size()` (bytes) is not the string's
on-screen column width, so a name with non-ASCII content gets padded to
the wrong width even when no truncation happens at all.

## Why libunistring, not utf8proc

`notcurses` (this project's UI toolkit) already depends on libunistring at
the shared-library level: `apt-cache depends libnotcurses-dev` lists
`Depends: libunistring5`, confirmed by `ldd` against the built
`libnotcurses-core.so`. `libunistring-dev` (headers) is also already
present on this dev machine. Using it instead of pulling in a second,
unrelated Unicode library (`utf8proc`) means no new *runtime* shared
library lands on any machine that can already run this project's UI - the
only new explicit build-dependency line is the `-dev` headers package
(`libunistring-dev` isn't auto-pulled by `libnotcurses-dev`, only the
runtime `libunistring5` is - so it still needs adding to the documented
dependency list, same as `utf8proc` would have).

The API is also a closer fit for this exact job:

- `u8_grapheme_next(const uint8_t *s, const uint8_t *end)` (`unigbrk.h`)
  returns a pointer straight to the start of the next grapheme cluster in
  a UTF-8 buffer - no need to hand-roll a codepoint-decode-plus-pairwise-
  `grapheme_break_stateful` loop the way `utf8proc` would require.
- `u8_width(const uint8_t *s, size_t n, const char *encoding)` /
  `u8_strwidth(const uint8_t *s, const char *encoding)` (`uniwidth.h`)
  give Unicode East-Asian-Width-aware column width for a UTF-8 byte range
  in one call, `encoding` (for resolving the "ambiguous width" class)
  filled from `locale_charset()` (`unistring/localcharset.h`, already
  pulled in transitively by `uniwidth.h`) - a natural fit for a terminal
  app that already assumes a UTF-8 locale throughout.

License: per `/usr/share/doc/libunistring-dev/copyright`, dual
LGPL-3+/GPL-2+ (individual generated headers like `uniwidth.h` carry an
LGPL-2.1+-or-later notice specifically). Dynamically linking an
LGPL library from an MIT-licensed project is unproblematic - the same
shape as the existing `libsndfile`/ALSA (LGPL-2.1+) entries already in
`THIRD_PARTY_LICENSES.md` - so this isn't a blocker, just a different
license family than `utf8proc`'s plain MIT.

## Proposed fix

### 1. New helper module: `src/util/Utf8.h` / `src/util/Utf8.cpp`

Header-only interface; `unigbrk.h`/`uniwidth.h` themselves only included
in the `.cpp` (the same "don't leak the third-party header" shape
`AmbisonicMagLSDecoder` uses for `mysofa.h`):

```cpp
// src/util/Utf8.h
namespace Utf8 {
  // Terminal-column display width of the whole (well-formed) UTF-8
  // string. Malformed byte sequences are handled the way libunistring
  // itself handles them (not rejected/thrown).
  int displayWidth(const std::string & text);

  // Right-truncates at the last grapheme-cluster boundary whose
  // cumulative display width is <= max_columns - never splits a
  // codepoint or a cluster (accent, ZWJ emoji sequence, ...).
  // max_columns <= 0 returns "".
  std::string truncateToWidth(const std::string & text, int max_columns);

  // Right-pads with ASCII spaces until displayWidth(text) == width; a
  // no-op if text is already that wide or wider. Never truncates -
  // combine with truncateToWidth() first when a call site needs both.
  std::string padToWidth(const std::string & text, int width);
}
```

Implementation sketch (`Utf8.cpp`):

- `displayWidth()`: `u8_strwidth(reinterpret_cast<const uint8_t*>(text.c_str()), locale_charset())` directly - one call, no manual loop.
- `truncateToWidth()`: walk grapheme clusters with `u8_grapheme_next()`
  from the string's start, calling `u8_width()` on each `[cluster_start,
  cluster_end)` byte range and accumulating; remember the byte offset
  after the last cluster that kept the running total `<= max_columns`,
  return `text.substr(0, that_offset)`. Stop (return what's accumulated
  so far) once `u8_grapheme_next()` returns `nullptr` (end of string).
- `padToWidth()`: `displayWidth(text)`, append `width - current` ASCII
  spaces if positive.

`locale_charset()` is cheap to call per-invocation (it self-caches
internally, per libunistring's own gnulib-derived convention) so there's
no need for this module to cache it itself.

### 2. Wire it into `PatternEditor::renderHeading()`

Replace each `if (... .size() > width) name.erase(width); else { while
(...) name += ' '; }` pair with:

```cpp
name = Utf8::truncateToWidth(name, text_width);
name = Utf8::padToWidth(name, text_width);
```

(`padToWidth()` is a no-op when truncation already hit the target width
exactly, so this is always safe to call unconditionally rather than
re-deriving the old if/else split.) Same replacement at all three call
sites listed above. The `std::max(0, actual_width - N)` column-budget
computations upstream of each site are unchanged - `text_width`/
`instrument_name_width`/`element_name_width` are already column counts,
which is exactly what the new functions expect.

### 3. Build wiring

- `CMakeLists.txt`: add `src/util/Utf8.cpp` to `synth_engine`'s source
  list (it has no dependency on notcurses/ALSA, so it belongs in the
  engine lib alongside `PatternBlockOps.cpp`/`PatternScroll.cpp` -
  `PatternEditor.cpp` itself stays musiceditor-only, calling into it).
  Find the library the same shape as the existing `mysofa` lookup (no
  pkg-config file ships with `libunistring-dev` on Ubuntu, confirmed -
  same as `mysofa`, hence `find_library`/`find_path` rather than
  `find_package`):
  `find_library(UNISTRING_LIBRARY unistring)` /
  `find_path(UNISTRING_INCLUDE_DIR unigbrk.h)`, then
  `target_link_libraries(synth_engine PUBLIC ${UNISTRING_LIBRARY})` and
  `target_include_directories(synth_engine PRIVATE ${UNISTRING_INCLUDE_DIR})`.
  Unlike `SYNTH_ENABLE_BINAURAL`, this is not proposed as an optional
  toggle - see "Open questions" below for why.
- `CLAUDE.md`'s Build section: add `libunistring-dev` to the Ubuntu
  dependency line (`libnotcurses-dev libnotcurses++-dev libfmt-dev
  libsndfile1-dev libasound2-dev` -> append `libunistring-dev`), and
  worth a short note there that its runtime half (`libunistring5`) is
  already pulled in transitively by `libnotcurses-dev`.
- `THIRD_PARTY_LICENSES.md`: add a row to the "Dynamically-linked system
  libraries" table - `| libunistring | LGPL-3+ / GPL-2+ |` - matching the
  existing `libmysofa`/`libsndfile`/etc. rows. No change needed to
  `ThirdPartyLicenses.h.in`/`--licenses` plumbing, it already embeds
  whatever the `.md` file says at build time.

### 4. Tests: `tests/Utf8Tests.cpp`

New `TEST(...)` cases (registered in `tests/CMakeLists.txt`'s
`synth_tests` source list), covering:

- Pure ASCII: `truncateToWidth`/`padToWidth` behave exactly like the old
  byte-based logic (regression safety net for the common case).
- A 4-byte astral codepoint (e.g. 𝄪, U+1D1AA, this codebase's own
  double-sharp glyph from `Note.h`) mid-string: truncating just before,
  just after, and exactly at its boundary never corrupts adjacent bytes
  and never splits the codepoint.
- A base character + combining accent (2 codepoints/1 grapheme
  cluster, e.g. `"e" + U+0301`): truncating to a width that would fit the
  base alone but not the cluster either drops the whole cluster or keeps
  it whole, never emits the base without its combining mark.
- A ZWJ sequence (e.g. a multi-codepoint emoji): same whole-cluster
  guarantee.
- `max_columns <= 0` returns `""`; `max_columns` larger than the string's
  own width returns the string unchanged.
- `padToWidth()` on a string already at or above the target width is a
  no-op; on a multi-byte string under the target it pads by the right
  *column* count, not byte count (the bug this whole plan fixes on the
  padding side).
- Malformed/invalid UTF-8 input doesn't hang or crash (defensive, not
  expected to occur from any real call site - track/instrument names are
  already constrained to whatever the XML parser accepted).

### 5. `docs/known_bugs.md`

Remove the "`PatternEditor::renderHeading()` truncates track/instrument
names by raw byte offset" entry once the fix lands - the file tracks
*open* bugs, not a changelog.

## Status

Implemented. One thing discovered during implementation, worth recording
here since it wasn't known when this plan was written: libunistring's
`u8_grapheme_next()` (a plain pairwise `uc_is_grapheme_break(a, b)`
underneath, no state beyond the two adjacent codepoints) doesn't implement
UAX #29's regional-indicator-pairing (flag emoji) or emoji-ZWJ-sequence
rules - both need to look past more than one adjacent pair, which
`utf8proc_grapheme_break_stateful()`'s explicit `state` parameter is built
for and libunistring's isn't. Verified directly against both libraries
with the same test strings (a flag emoji, an emoji+ZWJ+emoji sequence):
utf8proc merges each into one cluster, libunistring splits them into two.
Ordinary text - accented/combining characters, non-BMP codepoints - is
unaffected either way; only these two multi-emoji cluster rules are
missing. Decided to keep libunistring anyway (no new runtime dependency,
and track/instrument/SF2-preset names realistically never contain a flag
emoji or ZWJ sequence) rather than switch back to utf8proc for that
narrow case - see `docs/known_bugs.md` for the recorded gap and
`tests/Utf8Tests.cpp` for the tests asserting libunistring's actual
(verified, not assumed) behavior on both cases.

## Open questions / follow-ups worth deciding before implementing

- **Required dependency vs. optional-with-fallback.** `libmysofa` is
  optional (`SYNTH_ENABLE_BINAURAL`) because its absence degrades a
  well-defined *feature* (binaural decoding) to a working fallback
  (cardioid stereo). There's no equivalent graceful fallback for UTF-8
  truncation - "not UTF-8-aware" is a bug, not a feature toggle - and
  since the runtime half of this dependency (`libunistring5`) is already
  mandatory via notcurses, there's no real-world environment where this
  project builds/runs but `libunistring` is genuinely unavailable. This
  plan proposes it as a plain required dependency (unconditional
  `find_library`, `FATAL_ERROR` if absent), the same weight as
  `libfmt`/`libsndfile`, not an opt-in like `SYNTH_ENABLE_BINAURAL`.
- **Column-width edge cases beyond grapheme clustering.** `u8_width()`
  gives Unicode East-Asian-Width-aware answers (CJK characters are width
  2 under a CJK-locale `encoding`), which is more correct than the
  current byte-count logic even for single-cluster strings - but this
  plan doesn't attempt to audit every other place display width matters
  (e.g. anywhere else column budgets are computed against `.size()`);
  it's scoped to the three known-bug call sites in `renderHeading()`. A
  follow-up grep for other `.size()`-as-display-width call sites touching
  user-editable text (song/track names entered via `StatusLine`'s
  reader, say) would be worth doing separately once this lands, using
  `Utf8::displayWidth()` as the building block.
- **Normalization.** libunistring also offers full NFC/NFD normalization
  (`uninorm.h`, `u8_normalize()`); this plan doesn't propose normalizing
  stored names on load/save, only measuring and truncating them as
  authored. Two visually-identical names that differ only in
  composed-vs-decomposed form would still compare unequal anywhere the
  code does plain string equality - out of scope here.
