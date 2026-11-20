#include "TestFramework.h"

#include "../src/model/Chords.h"
#include "../src/model/Division.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/Note.h"
#include "../src/model/Song.h"
#include "../src/instruments/InstrumentProvider.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace chords;

namespace {

const int kC = 5 * 53; // C-4 in 53-EDO

std::vector<int> notes(std::initializer_list<int> steps) {
  std::vector<int> out;
  for (auto step : steps) out.push_back(kC + step);
  return out;
}

Result completeIn(Scale scale, std::initializer_list<int> given, int size, bool borrowed, Tuning tuning = Tuning::EDO53) {
  return complete(ScaleChords(scale, tuning, 0, borrowed), notes(given), size);
}

JustScale justOf(Scale scale) { return justScale(scale).value_or(JustScale{gamut::kExtendedDiamond}); }

std::size_t gamutIndexOf(Ratio ratio) {
  for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
    if (gamut::kExtendedDiamond[i].ratio == ratio) return i;
  }
  return gamut::kExtendedDiamond.size();
}

bool hasName(const Result & r, const std::string & name) {
  return std::any_of(r.chords.begin(), r.chords.end(), [&](const Completion & c) { return c.name == name; });
}

std::vector<Ratio> borrowedOf(Scale scale) {
  auto just = justOf(scale);
  std::vector<Ratio> out;
  for (auto & note : gamut::kExtendedDiamond) {
    if (!just.contains(note) && division::borrows(just.side.value_or(GamutSide::OTONAL), note.ratio)) out.push_back(note.ratio);
  }
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace

// The division gives each scale three of the six chords that take notes from both, and the notes
// each scale lacks in them are the ones it borrows.
TEST(borrowed_the_division_gives_each_scale_three_chords_and_three_notes) {
  int otonal = 0, utonal = 0;
  for (auto & chord : division::kChords) (chord.given == GamutSide::OTONAL ? otonal : utonal)++;
  CHECK(otonal == 3 && utonal == 3);
  auto sorted = [](std::vector<Ratio> v) {
    std::sort(v.begin(), v.end());
    return v;
  };
  CHECK(borrowedOf(Scale::OTONAL) == sorted({Ratio{60, 49}, Ratio{10, 7}, Ratio{12, 7}}));
  CHECK(borrowedOf(Scale::UTONAL) == sorted({Ratio{7, 6}, Ratio{7, 5}, Ratio{49, 30}}));
  // Every note of a divided chord is a note of the scale or borrowed.
  for (auto & chord : division::kChords) {
    auto just = justOf(chord.given == GamutSide::OTONAL ? Scale::OTONAL : Scale::UTONAL);
    auto borrowed = borrowedOf(chord.given == GamutSide::OTONAL ? Scale::OTONAL : Scale::UTONAL);
    for (auto ratio : division::notesOf(chord)) {
      auto index = gamutIndexOf(ratio);
      CHECK(index < gamut::kExtendedDiamond.size());
      CHECK(just.contains(gamut::kExtendedDiamond[index]) || std::find(borrowed.begin(), borrowed.end(), ratio) != borrowed.end());
    }
  }
}

// The scales keep their 18 notes; no borrowed note shares a key with a note of the scale in 53-EDO.
TEST(borrowed_notes_share_no_key_with_a_note_of_the_scale) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    auto just = justOf(scale);
    int own = 0;
    for (auto & note : gamut::kExtendedDiamond) own += just.contains(note) ? 1 : 0;
    CHECK(own == 18);
    for (auto ratio : borrowedOf(scale)) {
      int step = ((ratio.steps(53) % 53) + 53) % 53;
      for (auto & note : gamut::kExtendedDiamond) {
        if (!just.contains(note)) continue;
        CHECK(((note.ratio.steps(53) % 53) + 53) % 53 != step);
      }
    }
  }
}

TEST(borrowed_off_offers_no_note_outside_the_scale) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    ScaleChords off(scale, Tuning::EDO53, 0, false);
    auto just = justOf(scale);
    for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
      CHECK(!off.isBorrowed(i));
      CHECK(off.holds(i) == (just.contains(gamut::kExtendedDiamond[i]) && off.holds(i)));
      for (int size : {4, 5}) {
        for (auto * entry : off.chordsOf(i, size)) {
          for (auto note : entry->notes) CHECK(just.contains(gamut::kExtendedDiamond[note]));
        }
      }
    }
  }
  // With the setting on the same scale offers the borrowed notes.
  ScaleChords on(Scale::OTONAL, Tuning::EDO53, 0, true);
  CHECK(on.holds(gamutIndexOf(Ratio{10, 7})));
  ScaleChords off(Scale::OTONAL, Tuning::EDO53, 0, false);
  CHECK(!off.holds(gamutIndexOf(Ratio{10, 7})));
}

TEST(borrowed_on_changes_only_what_it_adds) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    for (int step = 0; step < 53; step++) {
      for (int width = 3; width <= 6; width++) {
        auto off = completeIn(scale, {step}, width, false);
        if (off.status != Status::OK) continue;
        // Every chord offered without the setting is still offered with it, or is the start of a
        // run that a borrowed note makes longer.
        auto on = completeIn(scale, {step}, width, true);
        for (auto & chord : off.chords) {
          CHECK(std::any_of(on.chords.begin(), on.chords.end(), [&](const Completion & c) { return c.notes.size() >= chord.notes.size() && std::equal(chord.notes.begin(), chord.notes.end(), c.notes.begin()) && std::equal(chord.ratios.begin(), chord.ratios.end(), c.ratios.begin()); }));
          CHECK(!chord.borrowed);
        }
      }
    }
  }
}

// One note played in octave 4 with a four-column cursor.
TEST(borrowed_on_gives_the_chords_with_a_borrowed_note) {
  enum { C = 0,
         A = 39,
         Gbv = 27,
         E = 17,
         Dbv = 5,
         Bbb = 41,
         Fbb = 15,
         Ab = 36,
         Eb = 14,
         Fsu = 26,
         B = 48,
         Ds = 12,
         Gdd = 38 };
  auto first = [](Scale scale, int note) { return completeIn(scale, {note}, 4, true).chords[0]; };
  CHECK(first(Scale::OTONAL, E).notes == notes({53 + E, 53 + C, A, Gbv}));
  CHECK(first(Scale::OTONAL, E).borrowed);
  CHECK(first(Scale::OTONAL, Dbv).notes == notes({53 + Dbv, Bbb, Gbv, Fbb}));
  CHECK(first(Scale::UTONAL, Ab).notes == notes({Ab, 53 + C, 53 + Eb, 53 + Fsu}));
  CHECK(first(Scale::UTONAL, B).notes == notes({B, 53 + Ds, 53 + Fsu, 53 + Gdd}));
  // Off, they write what they wrote.
  CHECK(!completeIn(Scale::OTONAL, {E}, 4, false).chords[0].borrowed);
  CHECK(completeIn(Scale::OTONAL, {E}, 4, false).chords[0].notes != first(Scale::OTONAL, E).notes);
}

TEST(borrowed_chords_are_marked_and_listed_from_other_notes) {
  enum { G = 31,
         F = 22 };
  // The B𝄫 harmonic seventh read from G (otonal), the D♯ subharmonic seventh read from F (utonal).
  auto g = completeIn(Scale::OTONAL, {G}, 4, true);
  CHECK(hasName(g, "B𝄫 harmonic seventh / G (borrowed)"));
  CHECK(!hasName(completeIn(Scale::OTONAL, {G}, 4, false), "B𝄫 harmonic seventh / G (borrowed)"));
  auto f = completeIn(Scale::UTONAL, {F}, 4, true);
  CHECK(hasName(f, "D♯ subharmonic seventh / F (borrowed)"));
  for (auto & chord : g.chords) CHECK(chord.borrowed == (chord.name.find("(borrowed)") != std::string::npos));
  // No chord offered without the setting is marked.
  for (auto & chord : completeIn(Scale::OTONAL, {G}, 4, false).chords) CHECK(!chord.borrowed);
}

// The diamond is not affected.
TEST(borrowed_does_not_change_the_diamond) {
  for (int step = 0; step < 53; step++) {
    auto off = completeIn(Scale::DIAMOND, {step}, 4, false);
    auto on = completeIn(Scale::DIAMOND, {step}, 4, true);
    CHECK(off.chords.size() == on.chords.size());
    for (std::size_t i = 0; i < off.chords.size() && i < on.chords.size(); i++) CHECK(off.chords[i].notes == on.chords[i].notes && !on.chords[i].borrowed);
  }
}

// In 31-EDO a borrowed note never takes the key of a note of the scale, and a chord is offered only
// if every note it needs is available.
TEST(borrowed_in_31_edo_never_takes_a_key_of_the_scale) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    ScaleChords off(scale, Tuning::EDO31, 0, false);
    ScaleChords on(scale, Tuning::EDO31, 0, true);
    for (int step = 0; step < 31; step++) {
      auto own = off.degreeOf(step);
      if (own) CHECK(on.degreeOf(step) == own);
    }
    for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
      if (!on.isBorrowed(i) || !on.holds(i)) continue;
      CHECK(!off.degreeOf(on.stepOf(i)));
    }
    for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
      for (auto * entry : on.chordsOf(i, 4)) {
        for (auto note : entry->notes) CHECK(on.holds(note));
      }
    }
  }
  // Otonal in 31-EDO keeps G♭↓ and B𝄫 but not F𝄫, which shares a key with a note of the scale.
  ScaleChords on(Scale::OTONAL, Tuning::EDO31, 0, true);
  CHECK(on.holds(gamutIndexOf(Ratio{10, 7})));
  CHECK(on.holds(gamutIndexOf(Ratio{12, 7})));
  CHECK(!on.holds(gamutIndexOf(Ratio{60, 49})));
}

// The setting belongs to the song: off by default, saved and restored, undoable, and a song file
// without it loads with it off.
TEST(borrowed_setting_is_in_the_song) {
  Song song;
  song.addTrack(std::make_unique<InstrumentTrack>(0));
  CHECK(!song.getBorrowedChords());
  song.setBorrowedChords(true);
  CHECK(song.getBorrowedChords());

  auto path = std::string(TESTS_SCRATCH_DIR) + "/borrowed_chords_round_trip.xml";
  song.save(path);
  Song reloaded;
  InstrumentProvider provider;
  CHECK(reloaded.open(path, provider));
  CHECK(reloaded.getBorrowedChords());
  // Another open song keeps its own.
  CHECK(!Song().getBorrowedChords());

  // Saved off, the file does not mention it and loads off.
  reloaded.setBorrowedChords(false);
  reloaded.save(path);
  Song again;
  CHECK(again.open(path, provider));
  CHECK(!again.getBorrowedChords());
  std::filesystem::remove(path);

  // It is an edit that can be undone.
  Song edited;
  edited.addTrack(std::make_unique<InstrumentTrack>(0));
  edited.setBorrowedChords(true);
  edited.undo();
  CHECK(!edited.getBorrowedChords());
}

// The step view's degrees of otonal, utonal and the diamond rise in every tuning.
TEST(borrowed_step_view_degrees_are_in_order) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL, Scale::DIAMOND}) {
    for (auto tuning : {Tuning::EDO12, Tuning::EDO19, Tuning::EDO31, Tuning::EDO53}) {
      Song song;
      song.setTuning(tuning);
      song.setScale(scale);
      auto degrees = song.getScaleDegreesWindow(0, 60);
      CHECK(std::is_sorted(degrees.begin(), degrees.end()));
    }
  }
}
