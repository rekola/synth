#ifndef _DIVISION_H_
#define _DIVISION_H_

#include "Gamut.h"
#include "Ratio.h"

#include <array>
#include <span>

// The division of the gamut's twenty seventh chords between the otonal and utonal
// scales. Each scale has seven chords of its own notes; six more take notes from
// both scales, and each scale is given three of them, borrowing the notes it
// lacks. A chord is named by its root: the lowest note of a harmonic seventh
// (4:5:6:7), the top note of a mirror seventh (1/7:1/6:1/5:1/4).
//
// This is the one table of the division. A scale's borrowed notes are the notes
// of the chords it is given that are not its own, so giving a chord to the other
// scale changes the scales, their tuning and their chords and nothing else.
namespace division {

struct DividedChord {
  Ratio root;
  bool harmonic;   // else a mirror seventh
  GamutSide given; // OTONAL or UTONAL: the scale that borrows its notes
};

inline constexpr std::array<DividedChord, 6> kChords = {{
    {Ratio{5, 4}, false, GamutSide::OTONAL},   // E−  : G♭↓ A C E
    {Ratio{15, 14}, false, GamutSide::OTONAL}, // D♭↓−: F𝄫 G♭↓ B𝄫 D♭↓
    {Ratio{12, 7}, true, GamutSide::OTONAL},   // B𝄫+ : B𝄫 D♭↓ F♭ G
    {Ratio{8, 5}, true, GamutSide::UTONAL},    // A♭+ : A♭ C E♭ F♯↑
    {Ratio{28, 15}, true, GamutSide::UTONAL},  // B+  : B D♯ F♯↑ G𝄪
    {Ratio{7, 6}, false, GamutSide::UTONAL},   // D♯− : F G♯ B D♯
}};

// The four notes of a chord, octave-reduced, the root among them.
constexpr std::array<Ratio, 4> notesOf(const DividedChord & chord) {
  if (chord.harmonic) {
    return {chord.root.reduced(), (chord.root * Ratio{5, 4}).reduced(), (chord.root * Ratio{3, 2}).reduced(), (chord.root * Ratio{7, 4}).reduced()};
  }
  return {(chord.root * Ratio{4, 7}).reduced(), (chord.root * Ratio{2, 3}).reduced(), (chord.root * Ratio{4, 5}).reduced(), chord.root.reduced()};
}

// Whether `side` borrows `ratio` (it is in a chord the table gives to the side).
constexpr bool borrows(std::span<const DividedChord> table, GamutSide side, Ratio ratio) {
  for (const auto & chord : table) {
    if (chord.given != side) continue;
    for (const auto & note : notesOf(chord)) {
      if (note == ratio) return true;
    }
  }
  return false;
}
constexpr bool borrows(GamutSide side, Ratio ratio) { return borrows(kChords, side, ratio); }

} // namespace division

#endif
