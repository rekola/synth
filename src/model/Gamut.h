#ifndef _GAMUT_H_
#define _GAMUT_H_

#include "Ratio.h"

#include <array>
#include <cstddef>
#include <string_view>

// The gamut: the one set of just ratios above the tonic that the otonal and
// utonal scales (Scale.h) are selections from, and that the tuner
// (JustIntonation.h) tunes from whatever scale a song is in. It is generated in
// rounds and is infinite in principle; kGamut holds rounds 0-3, in the order
// they are generated, so a later round only appends.
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
  // Index of the mirror note in kGamut (itself for 1/1).
  std::size_t mirror;
};

namespace gamut {
// The origin of each note is the mean it was made by, in the comment.
inline constexpr std::array<GamutNote, 19> kGamut = {{
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

constexpr bool mirrorsAreConsistent() {
  for (std::size_t i = 0; i < kGamut.size(); i++) {
    const auto & note = kGamut[i];
    if (note.mirror >= kGamut.size() || kGamut[note.mirror].mirror != i) return false;
    if (kGamut[note.mirror].ratio != note.ratio.mirror()) return false;
  }
  return true;
}
static_assert(mirrorsAreConsistent());

} // namespace gamut

#endif
