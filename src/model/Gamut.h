#ifndef _GAMUT_H_
#define _GAMUT_H_

#include "Ratio.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

// A gamut is a set of just ratios above the tonic that scales (Scale.h) are
// selections from, and that the tuner (JustIntonation.h) tunes from whatever
// scale a song is in. There are four: the extended diamond below (kExtendedDiamond),
// which the otonal and utonal scales select from, Ellis's duodene (kDuodene),
// and the Pythagorean gamut (kPythagorean), a chain of fifths.
//
// The extended diamond is the 7-limit tonality diamond (every ratio between two
// of 1, 3, 5 and 7, octave-reduced) plus 16/15, 15/14, 15/8 and 28/15: seventeen
// ratios on fifteen keys of 31 and 53-EDO. 16/15 and 15/14 share a key, as do
// 28/15 and 15/8 (225/224 apart); the song's scale decides which one the key
// carries. The gamut is closed under the mirror r -> 2/r, which swaps the sides.
// docs/extended-diamond.md has the construction.
enum class GamutSide {
  BOTH,   // 1/1, 4/3 and 3/2: in both scales
  OTONAL, // in the otonal scale only
  UTONAL, // in the utonal scale only
};

struct GamutNote {
  Ratio ratio;
  GamutSide side;
  // In Note::stringToKey()'s syntax, resolved by the song's tuning. The arrow
  // is one 53-EDO step and ignored elsewhere.
  std::string_view spelling;
  // Index of the mirror note in the same table (itself for 1/1), or
  // gamut::kNoMirror when the table is not closed under mirroring.
  std::size_t mirror;
};

namespace gamut {
inline constexpr std::size_t kNoMirror = static_cast<std::size_t>(-1);

// By ascending ratio. The otonal scale is the BOTH and OTONAL notes, the utonal
// scale the BOTH and UTONAL ones.
inline constexpr std::array<GamutNote, 17> kExtendedDiamond = {{
    {Ratio{1, 1}, GamutSide::BOTH, "C", 0},
    {Ratio{16, 15}, GamutSide::UTONAL, "D♭↓", 16},
    {Ratio{15, 14}, GamutSide::OTONAL, "D♭↓", 15},
    {Ratio{8, 7}, GamutSide::UTONAL, "E𝄫", 14},
    {Ratio{7, 6}, GamutSide::OTONAL, "D♯", 13},
    {Ratio{6, 5}, GamutSide::UTONAL, "E♭", 12},
    {Ratio{5, 4}, GamutSide::OTONAL, "E", 11},
    {Ratio{4, 3}, GamutSide::BOTH, "F", 10},
    {Ratio{7, 5}, GamutSide::OTONAL, "F♯↑", 9},
    {Ratio{10, 7}, GamutSide::UTONAL, "G♭↓", 8},
    {Ratio{3, 2}, GamutSide::BOTH, "G", 7},
    {Ratio{8, 5}, GamutSide::UTONAL, "A♭", 6},
    {Ratio{5, 3}, GamutSide::OTONAL, "A", 5},
    {Ratio{12, 7}, GamutSide::UTONAL, "B𝄫", 4},
    {Ratio{7, 4}, GamutSide::OTONAL, "A♯↑", 3},
    {Ratio{28, 15}, GamutSide::UTONAL, "B", 2},
    {Ratio{15, 8}, GamutSide::OTONAL, "B", 1},
}};

// The Pythagorean gamut: thirteen notes of the chain of fifths, six below the
// tonic and six above, octave-reduced and in the order of the chain. All are 3-limit. For 53-EDO only:
// 31-EDO cannot tune it under any spelling (81/64 lies between its steps 10
// and 11). The spellings are 53-EDO's names for each note's step, where the
// natural notes are the just major scale, so they are not always the
// chain-of-fifths names (given in the comments): the Pythagorean E, A and B♭
// are an arrow away from the natural ones, and the notes six fifths out are
// named for the nearer step.
inline constexpr std::array<GamutNote, 13> kPythagorean = {{
    {Ratio{1024, 729}, GamutSide::BOTH, "F♯↑", 12}, // chain: G♭, 6 fifths down
    {Ratio{256, 243}, GamutSide::BOTH, "C♯↑", 11},  // chain: D♭
    {Ratio{128, 81}, GamutSide::BOTH, "A♭↓", 10},   // chain: A♭
    {Ratio{32, 27}, GamutSide::BOTH, "E♭↓", 9},     // chain: E♭
    {Ratio{16, 9}, GamutSide::BOTH, "B♭↓", 8},      // chain: B♭
    {Ratio{4, 3}, GamutSide::BOTH, "F", 7},         // chain: F
    {Ratio{1, 1}, GamutSide::BOTH, "C", 6},
    {Ratio{3, 2}, GamutSide::BOTH, "G", 5},       // chain: G
    {Ratio{9, 8}, GamutSide::BOTH, "D", 4},       // chain: D
    {Ratio{27, 16}, GamutSide::BOTH, "A↑", 3},    // chain: A
    {Ratio{81, 64}, GamutSide::BOTH, "E↑", 2},    // chain: E
    {Ratio{243, 128}, GamutSide::BOTH, "B↑", 1},  // chain: B
    {Ratio{729, 512}, GamutSide::BOTH, "G♭↓", 0}, // chain: F♯, 6 fifths up
}};

// Ellis's duodene: twelve 5-limit notes, one per key of 12-EDO. B♭ is 9/5, not
// 16/9, so that in minor the v and ♭III triads are pure; F-B♭ is then 27/20,
// the comma any fixed twelve-note just scale leaves somewhere. By ascending
// ratio. The spellings mean these ratios in 31 and 53-EDO; the notes are not
// closed under mirroring (the mirror of 9/5 is 10/9).
inline constexpr std::array<GamutNote, 12> kDuodene = {{
    {Ratio{1, 1}, GamutSide::BOTH, "C", kNoMirror},
    {Ratio{16, 15}, GamutSide::BOTH, "D♭↓", kNoMirror},
    {Ratio{9, 8}, GamutSide::BOTH, "D", kNoMirror},
    {Ratio{6, 5}, GamutSide::BOTH, "E♭", kNoMirror},
    {Ratio{5, 4}, GamutSide::BOTH, "E", kNoMirror},
    {Ratio{4, 3}, GamutSide::BOTH, "F", kNoMirror},
    {Ratio{45, 32}, GamutSide::BOTH, "F♯↑", kNoMirror},
    {Ratio{3, 2}, GamutSide::BOTH, "G", kNoMirror},
    {Ratio{8, 5}, GamutSide::BOTH, "A♭", kNoMirror},
    {Ratio{5, 3}, GamutSide::BOTH, "A", kNoMirror},
    {Ratio{9, 5}, GamutSide::BOTH, "B♭", kNoMirror},
    {Ratio{15, 8}, GamutSide::BOTH, "B", kNoMirror},
}};

constexpr bool mirrorsAreConsistent(std::span<const GamutNote> table) {
  for (std::size_t i = 0; i < table.size(); i++) {
    const auto & note = table[i];
    if (note.mirror == kNoMirror) continue;
    if (note.mirror >= table.size() || table[note.mirror].mirror != i) return false;
    if (table[note.mirror].ratio != note.ratio.mirror()) return false;
  }
  return true;
}
static_assert(mirrorsAreConsistent(kExtendedDiamond));
static_assert(mirrorsAreConsistent(kPythagorean));

} // namespace gamut

#endif
