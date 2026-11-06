#ifndef _GAMUT_H_
#define _GAMUT_H_

#include "Ratio.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

// A gamut is a set of just ratios above the tonic that scales (Scale.h) are
// selections from, and that the tuner (JustIntonation.h) tunes from whatever
// scale a song is in. There are three: the superparticular gamut below
// (kSuperparticular), which the otonal and utonal scales select from,
// Partch's 7-limit tonality diamond (kDiamond7) and the Pythagorean gamut
// (kPythagorean), a chain of fifths.
//
// The superparticular gamut is generated in rounds and is infinite in
// principle; kSuperparticular holds rounds 0-3, in the order they are
// generated, so a later round only appends.
//
// Round 0: 1/1 (C) and its octave 2/1.
// Round 1: divide C-C': the arithmetic mean 3/2 (G), then the harmonic mean
//   4/3 (F).
// Round n >= 2: take the notes of round n-1 in the order they were created.
//   For each note X form the arithmetic and harmonic means of C-X, then of
//   X-C'. Keep a result r only if it belongs to a series:
//     otonal (arithmetic means): r is a harmonic of the F a fifth below C,
//       that is r = (k+1)/k with k = 2^a * 3^b (b <= 1), or r = 2k/(k+1) with
//       k+1 = 2^a * 3^b (b <= 1);
//     utonal (harmonic means): 2/r satisfies the same condition.
//   A result already in the gamut is skipped. Results are kept in the order
//   produced; from round 3 on that leaves exactly one per note.
// Every kept note's mirror (2/r, octave-reduced) is in the gamut too.
//
// Round 4 would add 25/24, 17/16, 23/12 and 31/16 and their mirrors 48/25,
// 32/17, 24/23 and 32/31.
enum class GamutSide {
  BOTH,   // 1/1, 3/2 and 4/3: in both series
  OTONAL, // an arithmetic mean
  UTONAL, // a harmonic mean
};

struct GamutNote {
  Ratio ratio;
  GamutSide side;
  int round;
  // In Note::stringToKey()'s syntax, resolved by the song's tuning. The arrow
  // is one 53-EDO step and ignored elsewhere. The apostrophe of C𝄫' puts it
  // below the octave's C rather than the tonic's, so it stays above 15/8.
  std::string_view spelling;
  // Index of the mirror note in the same table (itself for 1/1), or
  // gamut::kNoMirror when the table is not closed under mirroring.
  std::size_t mirror;
};

namespace gamut {
inline constexpr std::size_t kNoMirror = static_cast<std::size_t>(-1);

// The origin of each note is the mean it was made by, in the comment.
inline constexpr std::array<GamutNote, 19> kSuperparticular = {{
    {Ratio{1, 1}, GamutSide::BOTH, 0, "C", 0},
    {Ratio{3, 2}, GamutSide::BOTH, 1, "G", 2},        // arith. C-C'
    {Ratio{4, 3}, GamutSide::BOTH, 1, "F", 1},        // harm. C-C'
    {Ratio{5, 4}, GamutSide::OTONAL, 2, "E", 10},     // arith. C-G
    {Ratio{6, 5}, GamutSide::UTONAL, 2, "E♭", 9},     // harm. C-G
    {Ratio{7, 4}, GamutSide::OTONAL, 2, "A♯↑", 8},    // arith. G-C'
    {Ratio{12, 7}, GamutSide::UTONAL, 2, "B𝄫", 7},    // harm. G-C'
    {Ratio{7, 6}, GamutSide::OTONAL, 2, "D♯", 6},     // arith. C-F
    {Ratio{8, 7}, GamutSide::UTONAL, 2, "E𝄫", 5},     // harm. C-F
    {Ratio{5, 3}, GamutSide::OTONAL, 2, "A", 4},      // arith. F-C'
    {Ratio{8, 5}, GamutSide::UTONAL, 2, "A♭", 3},     // harm. F-C'
    {Ratio{9, 8}, GamutSide::OTONAL, 3, "D", 18},     // arith. C-E
    {Ratio{12, 11}, GamutSide::UTONAL, 3, "C𝄪", 17},  // harm. C-E♭
    {Ratio{15, 8}, GamutSide::OTONAL, 3, "B", 16},    // arith. A♯↑-C'
    {Ratio{24, 13}, GamutSide::UTONAL, 3, "B↓", 15},  // harm. B𝄫-C'
    {Ratio{13, 12}, GamutSide::OTONAL, 3, "D♭", 14},  // arith. C-D♯
    {Ratio{16, 15}, GamutSide::UTONAL, 3, "D♭↓", 13}, // harm. C-E𝄫
    {Ratio{11, 6}, GamutSide::OTONAL, 3, "C𝄫'", 12},  // arith. A-C'
    {Ratio{16, 9}, GamutSide::UTONAL, 3, "B♭↓", 11},  // harm. A♭-C'
}};

// Partch's 7-limit tonality diamond: n/d for n, d in {1, 3, 5, 7}, octave-reduced,
// by ascending ratio. No sides or rounds (all BOTH, round 0). Eleven notes are
// the superparticular gamut's, with the same spellings; 7/5 and 10/7 are new.
inline constexpr std::array<GamutNote, 13> kDiamond7 = {{
    {Ratio{1, 1}, GamutSide::BOTH, 0, "C", 0},
    {Ratio{8, 7}, GamutSide::BOTH, 0, "E𝄫", 12},
    {Ratio{7, 6}, GamutSide::BOTH, 0, "D♯", 11},
    {Ratio{6, 5}, GamutSide::BOTH, 0, "E♭", 10},
    {Ratio{5, 4}, GamutSide::BOTH, 0, "E", 9},
    {Ratio{4, 3}, GamutSide::BOTH, 0, "F", 8},
    {Ratio{7, 5}, GamutSide::BOTH, 0, "F♯↑", 7},
    {Ratio{10, 7}, GamutSide::BOTH, 0, "G♭↓", 6},
    {Ratio{3, 2}, GamutSide::BOTH, 0, "G", 5},
    {Ratio{8, 5}, GamutSide::BOTH, 0, "A♭", 4},
    {Ratio{5, 3}, GamutSide::BOTH, 0, "A", 3},
    {Ratio{12, 7}, GamutSide::BOTH, 0, "B𝄫", 2},
    {Ratio{7, 4}, GamutSide::BOTH, 0, "A♯↑", 1},
}};

// The Pythagorean gamut: thirteen notes of the chain of fifths, six below the
// tonic and six above, octave-reduced and in the order of the chain (the round
// is the distance from the tonic along it). All are 3-limit. For 53-EDO only:
// 31-EDO cannot tune it under any spelling (81/64 lies between its steps 10
// and 11). The spellings are 53-EDO's names for each note's step, where the
// natural notes are the just major scale, so they are not always the
// chain-of-fifths names (given in the comments): the Pythagorean E, A and B♭
// are an arrow away from the natural ones, and the notes six fifths out are
// named for the nearer step.
inline constexpr std::array<GamutNote, 13> kPythagorean = {{
    {Ratio{1024, 729}, GamutSide::BOTH, 6, "F♯↑", 12}, // chain: G♭, 6 fifths down
    {Ratio{256, 243}, GamutSide::BOTH, 5, "C♯↑", 11},  // chain: D♭
    {Ratio{128, 81}, GamutSide::BOTH, 4, "A♭↓", 10},   // chain: A♭
    {Ratio{32, 27}, GamutSide::BOTH, 3, "E♭↓", 9},     // chain: E♭
    {Ratio{16, 9}, GamutSide::BOTH, 2, "B♭↓", 8},      // chain: B♭
    {Ratio{4, 3}, GamutSide::BOTH, 1, "F", 7},         // chain: F
    {Ratio{1, 1}, GamutSide::BOTH, 0, "C", 6},
    {Ratio{3, 2}, GamutSide::BOTH, 1, "G", 5},       // chain: G
    {Ratio{9, 8}, GamutSide::BOTH, 2, "D", 4},       // chain: D
    {Ratio{27, 16}, GamutSide::BOTH, 3, "A↑", 3},    // chain: A
    {Ratio{81, 64}, GamutSide::BOTH, 4, "E↑", 2},    // chain: E
    {Ratio{243, 128}, GamutSide::BOTH, 5, "B↑", 1},  // chain: B
    {Ratio{729, 512}, GamutSide::BOTH, 6, "G♭↓", 0}, // chain: F♯, 6 fifths up
}};

// Ellis's duodene: twelve 5-limit notes, one per key of 12-EDO. B♭ is 9/5, not
// 16/9, so that in minor the v and ♭III triads are pure; F-B♭ is then 27/20,
// the comma any fixed twelve-note just scale leaves somewhere. By ascending
// ratio. The spellings mean these ratios in 31 and 53-EDO; the notes are not
// closed under mirroring (the mirror of 9/5 is 10/9).
inline constexpr std::array<GamutNote, 12> kDuodene = {{
    {Ratio{1, 1}, GamutSide::BOTH, 0, "C", kNoMirror},
    {Ratio{16, 15}, GamutSide::BOTH, 0, "D♭↓", kNoMirror},
    {Ratio{9, 8}, GamutSide::BOTH, 0, "D", kNoMirror},
    {Ratio{6, 5}, GamutSide::BOTH, 0, "E♭", kNoMirror},
    {Ratio{5, 4}, GamutSide::BOTH, 0, "E", kNoMirror},
    {Ratio{4, 3}, GamutSide::BOTH, 0, "F", kNoMirror},
    {Ratio{45, 32}, GamutSide::BOTH, 0, "F♯↑", kNoMirror},
    {Ratio{3, 2}, GamutSide::BOTH, 0, "G", kNoMirror},
    {Ratio{8, 5}, GamutSide::BOTH, 0, "A♭", kNoMirror},
    {Ratio{5, 3}, GamutSide::BOTH, 0, "A", kNoMirror},
    {Ratio{9, 5}, GamutSide::BOTH, 0, "B♭", kNoMirror},
    {Ratio{15, 8}, GamutSide::BOTH, 0, "B", kNoMirror},
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
static_assert(mirrorsAreConsistent(kSuperparticular));
static_assert(mirrorsAreConsistent(kDiamond7));
static_assert(mirrorsAreConsistent(kPythagorean));

} // namespace gamut

#endif
