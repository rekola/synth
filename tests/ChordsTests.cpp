#include "TestFramework.h"

#include "../src/model/Chords.h"
#include "../src/model/Note.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace chords;

namespace {

const int kC = 5 * 53; // C-4 in 53-EDO

// Steps of 53-EDO above C.
enum Step {
  C = 0,
  Dbb = 2,
  Dbv = 5,
  Ebb = 10,
  Ds = 12,
  Eb = 14,
  E = 17,
  F = 22,
  Fsu = 26,
  Gbv = 27,
  G = 31,
  Ab = 36,
  A = 39,
  Bbb = 41,
  Asu = 43,
  B = 48,
};

std::vector<int> notes(std::initializer_list<int> steps) {
  std::vector<int> out;
  for (auto step : steps) out.push_back(kC + step);
  return out;
}

Result completeIn(Scale scale, std::initializer_list<int> given, int size, Tuning tuning = Tuning::EDO53, int key = 0) {
  return complete(ScaleChords(scale, tuning, key), notes(given), size);
}

std::size_t gamutNote(Scale scale, const char * spelling) {
  auto side = scale == Scale::OTONAL ? GamutSide::OTONAL : GamutSide::UTONAL;
  for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
    auto & note = gamut::kExtendedDiamond[i];
    if (note.spelling == spelling && (note.side == GamutSide::BOTH || note.side == side)) return i;
  }
  CHECK(false);
  return 0;
}

std::vector<std::string> namesOf(Scale scale, const char * degree, int size = 4) {
  ScaleChords chords(scale, Tuning::EDO53, 0);
  std::vector<std::string> out;
  for (auto entry : chords.chordsOf(gamutNote(scale, degree), size)) out.push_back(chords.chordName(*entry));
  return out;
}

using NinthNames = std::vector<std::pair<const char *, std::vector<std::string>>>;
const NinthNames kOtonalNinthNames = {
    {"C", {"F harmonic ninth / C", "F major ninth / C", "C 7♯9 with fifth", "C harmonic ninth", "C subharmonic harmonic ninth", "C major ninth", "A subharmonic major ninth / C", "C subharmonic 7♯9 with fifth"}},
    {"D♭↓", {"A subharmonic harmonic ninth / D♭↓"}},
    {"D", {"C harmonic ninth / D", "C subharmonic harmonic ninth / D", "C major ninth / D"}},
    {"D♯", {"F harmonic ninth / D♯", "C 7♯9 with fifth / D♯", "C subharmonic 7♯9 with fifth / D♯"}},
    {"E", {"F major ninth / E", "C 7♯9 with fifth / E", "C harmonic ninth / E", "A subharmonic harmonic ninth / E", "C major ninth / E", "A subharmonic major ninth / E"}},
    {"F♭", {"C subharmonic harmonic ninth / F♭"}},
    {"F", {"F harmonic ninth", "F major ninth"}},
    {"F♯↑", {"C subharmonic 7♯9 with fifth / F♯↑"}},
    {"G", {"F harmonic ninth / G", "F major ninth / G", "C 7♯9 with fifth / G", "C harmonic ninth / G", "A subharmonic harmonic ninth / G", "C subharmonic harmonic ninth / G", "C major ninth / G", "A subharmonic major ninth / G", "C subharmonic 7♯9 with fifth / G"}},
    {"A", {"F harmonic ninth / A", "F major ninth / A", "A subharmonic harmonic ninth", "A subharmonic major ninth"}},
    {"A♯↑", {"C 7♯9 with fifth / A♯↑", "C harmonic ninth / A♯↑", "C subharmonic 7♯9 with fifth / A♯↑"}},
    {"B♭", {"C subharmonic harmonic ninth / B♭"}},
    {"B", {"A subharmonic harmonic ninth / B", "C major ninth / B", "A subharmonic major ninth / B"}},
};

const NinthNames kUtonalNinthNames = {
    {"C", {"F subharmonic harmonic ninth / C", "F subharmonic major ninth / C", "E𝄫 subharmonic 7♯9 with fifth / C", "B♭↓ harmonic ninth / C", "B♭↓ subharmonic harmonic ninth / C", "D♭↓ major ninth / C", "B♭↓ subharmonic major ninth / C", "E𝄫 7♯9 with fifth / C"}},
    {"D♭↓", {"D♭↓ harmonic ninth", "D♭↓ major ninth", "B♭↓ subharmonic major ninth / D♭↓"}},
    {"D↓", {"B♭↓ harmonic ninth / D↓"}},
    {"E𝄫", {"E𝄫 subharmonic 7♯9 with fifth", "B♭↓ subharmonic harmonic ninth / E𝄫", "E𝄫 7♯9 with fifth"}},
    {"E♭", {"F subharmonic harmonic ninth / E♭", "F subharmonic major ninth / E♭", "D♭↓ harmonic ninth / E♭", "D♭↓ major ninth / E♭"}},
    {"F", {"F subharmonic harmonic ninth", "F subharmonic major ninth", "E𝄫 subharmonic 7♯9 with fifth / F", "D♭↓ harmonic ninth / F", "B♭↓ harmonic ninth / F", "B♭↓ subharmonic harmonic ninth / F", "D♭↓ major ninth / F", "B♭↓ subharmonic major ninth / F", "E𝄫 7♯9 with fifth / F"}},
    {"G♭↓", {"E𝄫 7♯9 with fifth / G♭↓"}},
    {"G", {"F subharmonic harmonic ninth / G", "F subharmonic major ninth / G"}},
    {"G♯", {"B♭↓ harmonic ninth / G♯"}},
    {"A♭", {"F subharmonic major ninth / A♭", "E𝄫 subharmonic 7♯9 with fifth / A♭", "D♭↓ harmonic ninth / A♭", "B♭↓ subharmonic harmonic ninth / A♭", "D♭↓ major ninth / A♭", "B♭↓ subharmonic major ninth / A♭"}},
    {"B𝄫", {"F subharmonic harmonic ninth / B𝄫", "E𝄫 subharmonic 7♯9 with fifth / B𝄫", "E𝄫 7♯9 with fifth / B𝄫"}},
    {"B♭↓", {"B♭↓ harmonic ninth", "B♭↓ subharmonic harmonic ninth", "B♭↓ subharmonic major ninth"}},
    {"B", {"D♭↓ harmonic ninth / B"}},
};

} // namespace

TEST(chords_every_degree_of_both_scales_has_its_list) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    std::size_t total = 0;
    for (auto index : scaleGamutIndices(scale)) {
      auto & list = degreeChords(scale, index);
      CHECK(!list.empty());
      for (auto & entry : list) {
        CHECK(entry.notes[0] == index);
        CHECK(entry.notes.size() == 4 || entry.kind == Kind::MAJOR || entry.kind == Kind::MINOR);
        // No note twice.
        CHECK(std::set<std::size_t>(entry.notes.begin(), entry.notes.end()).size() == entry.notes.size());
      }
      total += list.size();
    }
    CHECK(total == (scale == Scale::OTONAL ? 45u : 49u));
  }
  CHECK(degreeChords(Scale::MAJOR, 0).empty());
}

TEST(chords_are_named_after_their_root) {
  // A harmonic seventh by its lowest note, a subharmonic seventh by its top note. A note's list is in
  // the order of its table: its seventh chords, a stand-alone triad, then the major sevenths and 7♯9.
  using V = std::vector<std::string>;
  CHECK(namesOf(Scale::OTONAL, "C") == (V{"C harmonic seventh", "F harmonic seventh / C", "A♯↑ subharmonic seventh / C", "F major seventh / C", "C major seventh", "C 7♯9"}));
  CHECK(namesOf(Scale::OTONAL, "D♯") == (V{"F harmonic seventh / D♯", "A♯↑ subharmonic seventh / D♯", "D♯ harmonic seventh", "C 7♯9 / D♯"}));
  CHECK(namesOf(Scale::OTONAL, "A") == (V{"F harmonic seventh / A", "A minor", "F major seventh / A"}));
  CHECK(namesOf(Scale::OTONAL, "D𝄪") == (V{"D𝄪 subharmonic seventh", "F♯↑ 7♯9 / D𝄪"}));
  CHECK(namesOf(Scale::OTONAL, "D♭↓") == (V{"B subharmonic seventh / D♭↓"}));
  CHECK(namesOf(Scale::OTONAL, "B") == (V{"B subharmonic seventh", "C major seventh / B"}));
  CHECK(namesOf(Scale::UTONAL, "C") == (V{"C subharmonic seventh", "G subharmonic seventh / C", "E𝄫 harmonic seventh / C", "A♭ major seventh / C", "D♭↓ major seventh / C", "E𝄫 7♯9 / C"}));
  CHECK(namesOf(Scale::UTONAL, "A♭") == (V{"C subharmonic seventh / A♭", "D♭↓ harmonic seventh / A♭", "A♭ major", "A♭ major seventh", "D♭↓ major seventh / A♭", "G𝄫 7♯9 / A♭"}));
  CHECK(namesOf(Scale::UTONAL, "D♭↓") == (V{"D♭↓ harmonic seventh", "D♭↓ major seventh"}));
  CHECK(namesOf(Scale::UTONAL, "B♯") == (V{"B𝄫 subharmonic seventh / B♯", "B♯ 7♯9"}));
  CHECK(namesOf(Scale::UTONAL, "B") == (V{"D♭↓ harmonic seventh / B"}));
  CHECK(namesOf(Scale::UTONAL, "E𝄫") == (V{"C subharmonic seventh / E𝄫", "E𝄫 harmonic seventh", "B𝄫 subharmonic seventh / E𝄫", "E𝄫 7♯9", "B♯ 7♯9 / E𝄫"}));
}

TEST(chords_one_note_is_completed_to_the_first_notes_of_its_first_chord) {
  auto four = completeIn(Scale::OTONAL, {C}, 4);
  CHECK(four.status == Status::OK);
  CHECK(four.chords[0].notes == notes({C, E, G, Asu}));
  CHECK(four.chords[0].name == "C harmonic seventh");
  CHECK(four.chords.size() == 6);
  // Then the list in the order of its table: the other seventh chords C belongs to, the major sevenths, the 7♯9.
  CHECK(four.chords[1].name == "F harmonic seventh / C");
  CHECK(four.chords[2].name == "A♯↑ subharmonic seventh / C");
  CHECK(four.chords[3].name == "F major seventh / C");
  CHECK(four.chords[4].name == "C major seventh");
  CHECK(four.chords[5].name == "C 7♯9");

  auto three = completeIn(Scale::OTONAL, {C}, 3);
  CHECK(three.chords[0].notes == notes({C, E, G}));
  CHECK(three.chords[0].name == "C major");
  // The alternatives in list order; C E G of the major seventh repeats the first and is dropped.
  CHECK(three.chords.size() == 5);
  CHECK(three.chords[1].notes == notes({C, Ds, F}));
  CHECK(three.chords[1].name == "C narrow major");
  // A subharmonic seventh hangs from the played note's octave up, so its three notes at a width of
  // three are the top three: F♯↑ A♯↑ C, ending an octave above the played C.
  // The played note is in the first column and a subharmonic chord falls from it.
  CHECK(three.chords[2].notes == notes({53 + C, Asu, Fsu}));
  CHECK(three.chords[2].name == "A♯↑ subharmonic seventh / C (3 notes)");
  CHECK(three.chords[3].notes == notes({C, E, F}));
  CHECK(three.chords[3].name == "F major seventh / C (3 notes)");
  CHECK(three.chords[4].notes == notes({C, Ds, E}));
  CHECK(three.chords[4].name == "C 7♯9 (3 notes)");

  auto two = completeIn(Scale::OTONAL, {C}, 2);
  CHECK(two.chords.size() == 3);
  CHECK(two.chords[0].notes == notes({C, E}));
  CHECK(two.chords[0].name == "C harmonic seventh (2 notes)");
  CHECK(two.chords[1].notes == notes({C, Ds}));
  CHECK(two.chords[2].notes == notes({53 + C, Asu})); // the subharmonic seventh, from its top note

  auto a = completeIn(Scale::OTONAL, {A}, 3);
  CHECK(a.chords[0].notes == notes({A, 53 + C, 53 + Ds}));
  CHECK(a.chords[0].name == "A septimal diminished");
  // The minor triad is an upward chord: A C E from A.
  CHECK(a.chords[1].notes == notes({A, 53 + C, 53 + E}));
  CHECK(a.chords[1].name == "A minor");

  auto utonal = completeIn(Scale::UTONAL, {C}, 4);
  CHECK(utonal.chords[0].notes == notes({53 + C, Ab, F, Ebb}));
  CHECK(utonal.chords[0].name == "C subharmonic seventh");
  auto utonal_three = completeIn(Scale::UTONAL, {C}, 3);
  CHECK(utonal_three.chords[0].name == "F minor"); // F A♭ C, from its top note C
  CHECK(std::any_of(utonal_three.chords.begin(), utonal_three.chords.end(), [](const Completion & c) { return c.name == "C narrow minor"; }));
  CHECK(completeIn(Scale::UTONAL, {Ab}, 3).chords[0].name == "A♭ major"); // the triad A♭ names comes first
}

TEST(chords_several_given_notes_pick_the_chords_that_contain_them) {
  auto c_ds = completeIn(Scale::OTONAL, {C, Ds}, 4);
  CHECK(c_ds.chords.size() == 3);
  CHECK(c_ds.chords[0].notes == notes({C, Ds, F, A}));
  CHECK(c_ds.chords[0].name == "F harmonic seventh / C");
  CHECK(c_ds.chords[1].notes == notes({C, Ds, E, Asu}));
  CHECK(c_ds.chords[1].name == "C 7♯9");
  // The subharmonic seventh is found from the highest given note, D♯, and hangs from it: the played
  // notes keep their octaves and nothing is added above D♯.
  CHECK(c_ds.chords[2].notes == notes({Ds, C, Asu - 53, Fsu - 53}));
  CHECK(c_ds.chords[2].name == "A♯↑ subharmonic seventh / D♯");

  // A given note is never left out; the rest fill from the chord's lowest.
  auto c_as = completeIn(Scale::OTONAL, {C, Asu}, 3);
  CHECK(c_as.chords[0].notes == notes({C, E, Asu}));
  CHECK(c_as.chords[0].name == "C harmonic seventh (3 notes)");

  // Upward from the lowest given note, whatever the chord's usual lowest is.
  auto e_as = completeIn(Scale::OTONAL, {E, Asu}, 4);
  CHECK(e_as.chords.size() == 2);
  CHECK(e_as.chords[0].notes == notes({E, G, Asu, 53 + C}));
  CHECK(e_as.chords[0].name == "C harmonic seventh / E");
  CHECK(e_as.chords[1].notes == notes({E, Asu, 53 + C, 53 + Ds}));
  CHECK(e_as.chords[1].name == "C 7♯9 / E");

  // Given notes keep their octaves; added ones lie within an octave above the lowest.
  auto wide = complete(ScaleChords(Scale::OTONAL, Tuning::EDO53, 0), {kC + E, kC + 2 * 53 + C}, 4);
  CHECK(wide.chords[0].notes == (std::vector<int>{kC + E, kC + G, kC + Asu, kC + 2 * 53 + C}));

  // A short chord is not padded.
  auto minor = completeIn(Scale::OTONAL, {A, 53 + E}, 4);
  // (The minor triad hangs from the highest played note, E, and follows the upward chord.)
  CHECK(minor.chords[0].notes == notes({A, 53 + C, 53 + E}));
  CHECK(minor.chords[0].name == "A minor");
  CHECK(minor.chords[1].notes == notes({A, 53 + C, 53 + E, 53 + F}));
}

TEST(chords_nothing_is_added_without_a_chord) {
  CHECK(completeIn(Scale::OTONAL, {C, G, A}, 4).status == Status::NO_CHORD);
  CHECK(completeIn(Scale::OTONAL, {C, E, G}, 3).status == Status::NOTHING_TO_ADD);
  CHECK(completeIn(Scale::OTONAL, {C, E, G}, 2).status == Status::NOTHING_TO_ADD);
  CHECK(completeIn(Scale::OTONAL, {C}, 1).status == Status::NOTHING_TO_ADD);
  CHECK(completeIn(Scale::OTONAL, {1}, 4).status == Status::NOT_A_DEGREE); // C↑ is no note of the scale
  CHECK(completeIn(Scale::OTONAL, {C, 1}, 4).status == Status::NO_CHORD);
  CHECK(completeIn(Scale::MAJOR, {C}, 4).status == Status::NO_SCALE);
  CHECK(completeIn(Scale::NONE, {C}, 4).status == Status::NO_SCALE);
  CHECK(completeIn(Scale::OTONAL, {C}, 4, Tuning::PERCUSSION).status == Status::NO_SCALE);
}

TEST(chords_follow_the_tonic_and_the_tuning) {
  // On D (step 9 of 53-EDO) the tonic's key completes to the same shape, named by the tuning's keys.
  auto on_d = completeIn(Scale::OTONAL, {9}, 4, Tuning::EDO53, 9);
  CHECK(on_d.chords[0].notes == notes({9, 9 + E, 9 + G, 9 + Asu}));
  CHECK(on_d.chords[0].name == "D harmonic seventh");
  CHECK(completeIn(Scale::OTONAL, {C}, 4, Tuning::EDO53, 9).status == Status::NOT_A_DEGREE); // C is 16/9 above D

  // 31-EDO holds the whole scale: C E G A♯ are steps 0, 10, 18, 25.
  ScaleChords edo31(Scale::OTONAL, Tuning::EDO31, 0);
  auto in31 = complete(edo31, {5 * 31}, 4);
  CHECK(in31.status == Status::OK);
  CHECK(in31.chords[0].notes == (std::vector<int>{155, 165, 173, 180}));

  // 12-EDO cannot hold every note; a chord is offered only with notes it holds, each on a key of its own.
  ScaleChords edo12(Scale::OTONAL, Tuning::EDO12, 0);
  std::size_t offered = 0;
  for (auto index : scaleGamutIndices(Scale::OTONAL)) {
    for (auto entry : edo12.chordsOf(index)) {
      std::set<int> steps;
      for (auto note : entry->notes) {
        steps.insert(edo12.stepOf(note));
        CHECK(edo12.degreeOf(edo12.stepOf(note)) == note);
      }
      CHECK(steps.size() == entry->notes.size());
      offered++;
    }
  }
  CHECK(offered > 0 && offered < 45);
}

TEST(chords_triads_are_named_by_their_ratios) {
  CHECK(triadName(Ratio{5, 4}, Ratio{3, 2}) == "major");
  CHECK(triadName(Ratio{6, 5}, Ratio{3, 2}) == "minor");
  CHECK(triadName(Ratio{7, 6}, Ratio{4, 3}) == "narrow major");
  CHECK(triadName(Ratio{8, 7}, Ratio{4, 3}) == "narrow minor");
  CHECK(triadName(Ratio{6, 5}, Ratio{7, 5}) == "septimal diminished");
  CHECK(triadName(Ratio{7, 6}, Ratio{7, 5}) == "subharmonic diminished");
  CHECK(!triadName(Ratio{5, 4}, Ratio{4, 3}));
}

TEST(chords_every_degree_has_its_five_note_chords_in_order) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    std::size_t total = 0;
    for (auto & [degree, expected] : scale == Scale::OTONAL ? kOtonalNinthNames : kUtonalNinthNames) {
      CHECK(namesOf(scale, degree, 5) == expected);
      total += expected.size();
      for (auto & entry : degreeChords(scale, gamutNote(scale, degree), 5)) {
        CHECK(entry.notes.size() == 5);
        CHECK(entry.notes[0] == gamutNote(scale, degree));
      }
    }
    CHECK(total == 45u);
  }
  CHECK(degreeChords(Scale::MAJOR, 0, 5).empty());
}

TEST(chords_five_columns_hold_a_seventh_chord_and_the_ninths_follow) {
  auto f = completeIn(Scale::OTONAL, {F}, 5);
  // The run from F comes first: F A C D♯ F, the harmonic seventh with its octave (a name does not
  // depend on the voicing).
  CHECK(f.chords[0].notes == notes({F, A, 53 + C, 53 + Ds, 53 + F}));
  CHECK(f.chords[0].name == "F harmonic seventh");
  auto has = [&](const Result & r, const char * name, std::initializer_list<int> want) {
    return std::any_of(r.chords.begin(), r.chords.end(), [&](const Completion & c) { return c.name == name && c.notes == notes(want); });
  };
  // The ninth chords stay in the list, after the seventh chords.
  CHECK(has(f, "F harmonic ninth", {F, G, A, 53 + C, 53 + Ds}));
  CHECK(has(f, "F major ninth", {F, G, A, 53 + C, 53 + E}));
  CHECK(f.chords[1].name == "F major seventh");

  auto c = completeIn(Scale::OTONAL, {C}, 5);
  CHECK(c.chords[0].notes == notes({C, E, G, Asu, 53 + C}));
  CHECK(c.chords[0].name == "C harmonic seventh");
  CHECK(has(c, "C 7♯9 with fifth", {C, Ds, E, G, Asu}));

  // F♯↑ has no run: a seventh chord that contains it, read from it, with its first note repeated an octave on.
  auto fsu = completeIn(Scale::OTONAL, {Fsu}, 5);
  CHECK(fsu.chords[0].notes.size() == 5);
  // The subharmonic seventh falls from F♯↑'s octave above, with that note an octave lower at the end.
  CHECK(fsu.chords[0].notes[4] == fsu.chords[0].notes[0] - 53);
  CHECK(fsu.chords[0].name == "A♯↑ subharmonic seventh / F♯↑");
  CHECK(has(fsu, "F♯↑ 7♯9", {26, 38, 43, 69, 79}));
  CHECK(has(fsu, "C subharmonic 7♯9 with fifth / F♯↑", {53 + Fsu, 53 + Ds, 53 + C, Asu, G}));

  auto cb = completeIn(Scale::OTONAL, {C, B}, 5);
  CHECK(cb.chords[0].name == "C major seventh");
  CHECK(has(cb, "C major ninth", {C, 9, E, G, B}));
}

TEST(chords_a_degree_without_five_note_chords_falls_back_to_four) {
  // D𝄫 (step 2 in 53-EDO) has none.
  auto dbb = completeIn(Scale::OTONAL, {Dbb}, 5);
  CHECK(dbb.status == Status::OK);
  auto four = completeIn(Scale::OTONAL, {Dbb}, 4);
  CHECK(dbb.chords.size() == four.chords.size());
  // The seventh chord with its first note an octave on.
  auto first = four.chords[0].notes;
  first.push_back(first[0] + 53);
  CHECK(dbb.chords[0].notes == first);
}

TEST(chords_five_note_chords_are_not_offered_at_narrower_widths) {
  for (int size : {2, 3, 4}) {
    for (auto & chord : completeIn(Scale::OTONAL, {C}, size).chords) CHECK(static_cast<int>(chord.notes.size()) <= size);
    for (auto & chord : completeIn(Scale::OTONAL, {F}, size).chords) CHECK(chord.name.find("ninth") == std::string::npos);
  }
  // A width beyond the largest chord is the largest.
  CHECK(completeIn(Scale::OTONAL, {F}, 7).chords[0].name == "F harmonic ninth"); // the run to 9
  // As many given notes as the width: nothing to add.
  CHECK(completeIn(Scale::OTONAL, {C, Ds, F, G, A}, 5).status == Status::NOTHING_TO_ADD);
  // Four given notes of a five-note chord are completed by it.
  auto four = completeIn(Scale::OTONAL, {C, Ds, F, G}, 5);
  CHECK(four.status == Status::OK);
  CHECK(four.chords[0].notes == notes({C, Ds, F, G, A}));
}

TEST(chords_utonal_five_note_chords_complete_from_a_degree) {
  auto c = completeIn(Scale::UTONAL, {C}, 5);
  // The chords C names (as their top note) come first: a subharmonic chord hung from an octave above the played C.
  CHECK(c.chords[0].notes == notes({53 + C, Ab, F, Ebb, C}));
  CHECK(c.chords[0].name == "C subharmonic seventh");
  CHECK(std::any_of(c.chords.begin(), c.chords.end(), [](const Completion & chord) { return chord.name == "E𝄫 subharmonic 7♯9 with fifth / C" && chord.notes == notes({53 + C, Bbb, Ab, F, Ebb}); }));
  CHECK(std::any_of(c.chords.begin(), c.chords.end(), [](const Completion & chord) { return chord.name == "F subharmonic harmonic ninth / C" && chord.notes == notes({53 + C, Bbb, G, F, Eb}); }));
}

// A given note whose ratio the caller knows is that ratio, whatever its key holds; the
// completion says each note's ratio.
TEST(complete_takes_the_ratio_of_a_given_note_when_it_is_known) {
  ScaleChords scale(Scale::OTONAL, Tuning::EDO53, 39); // A otonal
  auto tonic = gamutNote(Scale::OTONAL, "C");
  // The note value of A is the tonic whatever its name; no ratio needs working out from it.
  auto result = complete(scale, {kC + 39}, 4, {{kC + 39, tonic}});
  CHECK(result.status == Status::OK);
  CHECK(result.chords[0].notes == (std::vector<int>{kC + 39, kC + 39 + E, kC + 39 + G, kC + 39 + Asu}));
  CHECK(result.chords[0].ratios.size() == 4);
  CHECK(result.chords[0].ratios[0] == tonic);
  CHECK(result.chords[0].ratios[1] == gamutNote(Scale::OTONAL, "E"));
  // Without it, the key's own note is used: the same chord.
  CHECK(complete(scale, {kC + 39}, 4).chords[0].notes == result.chords[0].notes);
}

// ---- Subharmonic chords are built downward from the played note ----

TEST(every_kind_of_chord_has_a_direction_and_the_subharmonic_form_of_an_upward_kind_is_downward) {
  const std::vector<std::pair<Kind, Direction>> kinds = {
      {Kind::HARMONIC_SEVENTH, Direction::UPWARD},
      {Kind::SUBHARMONIC_SEVENTH, Direction::DOWNWARD},
      {Kind::MAJOR_SEVENTH, Direction::UPWARD},
      {Kind::SEVEN_SHARP_NINE, Direction::UPWARD},
      {Kind::MAJOR, Direction::UPWARD},
      {Kind::MINOR, Direction::UPWARD},
      {Kind::HARMONIC_NINTH, Direction::UPWARD},
      {Kind::SUBHARMONIC_HARMONIC_NINTH, Direction::DOWNWARD},
      {Kind::MAJOR_NINTH, Direction::UPWARD},
      {Kind::SUBHARMONIC_MAJOR_NINTH, Direction::DOWNWARD},
      {Kind::SEVEN_SHARP_NINE_FIFTH, Direction::UPWARD},
      {Kind::SUBHARMONIC_SEVEN_SHARP_NINE_FIFTH, Direction::DOWNWARD},
  };
  for (auto & [kind, direction] : kinds) CHECK(directionOf(kind) == direction);
}

namespace {
std::vector<int> relative(const Completion & chord) {
  std::vector<int> out;
  for (auto note : chord.notes) out.push_back(note - kC);
  return out;
}
} // namespace

// A subharmonic chord is written with its top note an octave above the played note, and nothing above that.
TEST(a_subharmonic_chord_is_written_downward_from_the_played_note) {
  // utonal C: E𝄫4 F4 A♭4 C5
  auto c = completeIn(Scale::UTONAL, {C}, 4);
  CHECK(relative(c.chords[0]) == (std::vector<int>{53 + C, Ab, F, Ebb}));
  // utonal G: B𝄫4 C5 E♭5 G5
  auto g = completeIn(Scale::UTONAL, {G}, 4);
  CHECK(relative(g.chords[0]) == (std::vector<int>{53 + G, 53 + Eb, 53 + C, Bbb}));
  CHECK(c.chords[0].notes.front() == kC + 53 + C); // nothing above the top note, which is in the first column
  // otonal C: unchanged, C4 E4 G4 A♯↑4.
  CHECK(relative(completeIn(Scale::OTONAL, {C}, 4).chords[0]) == (std::vector<int>{C, E, G, Asu}));
  // An upward chord of utonal is as before: E𝄫4 G♭↓4 B𝄫4 C5 is one of E𝄫's candidates.
  auto ebb = completeIn(Scale::UTONAL, {Ebb}, 4);
  CHECK(std::any_of(ebb.chords.begin(), ebb.chords.end(), [](const Completion & chord) {
    return relative(chord) == (std::vector<int>{Ebb, Gbv, Bbb, 53 + C}) && chord.name == "E𝄫 harmonic seventh";
  }));
}

// A chord with more notes than the cursor is wide is cut from its far end: a subharmonic chord keeps the played
// note and the notes just below it.
TEST(a_subharmonic_chord_is_cut_to_the_width_from_the_bottom) {
  auto three = completeIn(Scale::UTONAL, {C}, 3);
  CHECK(relative(three.chords[0]) == (std::vector<int>{53 + C, Ab, F}));
  auto two = completeIn(Scale::UTONAL, {C}, 2);
  CHECK(relative(two.chords[0]) == (std::vector<int>{53 + C, Ab}));
  // An upward chord keeps the played note and the notes just above it, as before.
  CHECK(relative(completeIn(Scale::OTONAL, {C}, 3).chords[0]) == (std::vector<int>{C, E, G}));
}

// A downward chord in a note's list is read downward from the note, even where the note is not its top
// note, and the played note is in the first column.
TEST(a_downward_chord_in_the_list_is_read_downward_from_the_note) {
  auto c = completeIn(Scale::OTONAL, {C}, 3);
  CHECK(std::any_of(c.chords.begin(), c.chords.end(), [](const Completion & chord) {
    return relative(chord) == (std::vector<int>{53 + C, Asu, Fsu}) && chord.name == "A♯↑ subharmonic seventh / C (3 notes)";
  }));
  // The minor triad is upward: A C E from A, and from E it is the run at a width of three.
  auto a = completeIn(Scale::OTONAL, {A}, 3);
  CHECK(std::any_of(a.chords.begin(), a.chords.end(), [](const Completion & chord) { return relative(chord) == (std::vector<int>{A, 53 + C, 53 + E}) && chord.name == "A minor"; }));
  CHECK(relative(completeIn(Scale::OTONAL, {E}, 3).chords[0]) == (std::vector<int>{53 + E, 53 + C, A}));
}

// ---- Phase 2: several played notes, the order of the lists, the range limit ----

// With one played note, the chords the note names come first, in its scale: a subharmonic chord hangs from an
// octave above the played note.
TEST(a_note_gives_the_chord_it_is_heard_as) {
  // otonal A♯↑ names the subharmonic seventh C D♯ F♯↑ A♯↑, the first column holding the top note: A♯↑5 F♯↑5 D♯5 C5
  CHECK(relative(completeIn(Scale::OTONAL, {Asu}, 4).chords[0]) == (std::vector<int>{53 + Asu, 53 + Fsu, 53 + Ds, 53 + C}));
  // otonal B names D♭↓ E G B: B5 G5 E5 D♭↓5
  CHECK(relative(completeIn(Scale::OTONAL, {B}, 4).chords[0]) == (std::vector<int>{53 + B, 53 + G, 53 + E, 53 + Dbv}));
  // otonal D names F♭ G B♭ D: D5 B♭4 G4 F♭4
  CHECK(relative(completeIn(Scale::OTONAL, {9}, 4).chords[0]) == (std::vector<int>{53 + 9, 45, G, 19}));
  // otonal E: its run falls E5 C5 A4 and stops, since G♭↓ is no note of otonal
  auto e = completeIn(Scale::OTONAL, {E}, 4);
  // The width chooses the class: a seventh chord that contains E, read from E, at four columns.
  CHECK(e.chords[0].name == "C harmonic seventh / E");
  auto e3 = completeIn(Scale::OTONAL, {E}, 3);
  CHECK(relative(e3.chords[0]) == (std::vector<int>{53 + E, 53 + C, A}));
  CHECK(e3.chords[0].name == "A minor");
  // diamond E: G♭↓ A C E from the top, E5 C5 A4 G♭↓4, and cut to three notes E5 C5 A4
  CHECK(relative(completeIn(Scale::DIAMOND, {E}, 4).chords[0]) == (std::vector<int>{53 + E, 53 + C, A, Gbv}));
  CHECK(relative(completeIn(Scale::DIAMOND, {E}, 3).chords[0]) == (std::vector<int>{53 + E, 53 + C, A}));
}

// A note's list is its table: in the diamond the otonal chords come first, in utonal the utonal ones.
TEST(a_notes_list_starts_with_its_scales_own_chords) {
  auto first_two = [](Scale scale) {
    ScaleChords chords(scale, Tuning::EDO53, 0);
    std::vector<std::string> out;
    auto list = chords.chordsOf(0);
    for (std::size_t i = 0; i < 2 && i < list.size(); i++) out.push_back(chords.chordName(*list[i]));
    return out;
  };
  using V = std::vector<std::string>;
  CHECK(first_two(Scale::DIAMOND) == (V{"C harmonic seventh", "F harmonic seventh / C"}));
  CHECK(first_two(Scale::UTONAL)[0] == "C subharmonic seventh");
  CHECK(first_two(Scale::OTONAL)[0] == "C harmonic seventh");
}

// Several played notes: the upward chord is found from the lowest, the downward chord from the highest, and each
// contains every played note.
TEST(several_played_notes_find_upward_chords_from_the_lowest_and_downward_chords_from_the_highest) {
  // utonal: F and the C above it write E𝄫 F A♭ C with that C on top; F and C keep their octaves.
  auto f_c = completeIn(Scale::UTONAL, {F, 53 + C}, 4);
  CHECK(f_c.status == Status::OK);
  CHECK(relative(f_c.chords[0]) == (std::vector<int>{53 + C, Ab, F, Ebb}));
  CHECK(f_c.chords[0].name == "C subharmonic seventh");
  // Nothing is added above the highest played note.
  for (auto & chord : f_c.chords) {
    for (auto note : chord.notes) CHECK(note <= kC + 53 + C || chord.notes.front() != kC + 53 + C);
  }
  // otonal: C and D♯ give upward chords from C, then the subharmonic seventh from D♯, in which every note but
  // the played ones is below D♯.
  auto c_ds = completeIn(Scale::OTONAL, {C, Ds}, 4);
  for (auto & chord : c_ds.chords) {
    for (auto played : {kC + C, kC + Ds}) CHECK(std::find(chord.notes.begin(), chord.notes.end(), played) != chord.notes.end());
  }
  bool hangs = false;
  for (auto & chord : c_ds.chords) {
    if (chord.name == "A♯↑ subharmonic seventh / D♯") {
      hangs = true;
      CHECK(relative(chord) == (std::vector<int>{Ds, C, Asu - 53, Fsu - 53})); // D♯ is the top, in the first column
    }
  }
  CHECK(hangs);
  // The chord must contain every played note: the notes C, G and A are in no chord together.
  CHECK(completeIn(Scale::OTONAL, {C, G, A}, 4).status == Status::NO_CHORD);
}

// A downward chord that would need a note below the lowest the track can hold is not offered.
TEST(a_downward_chord_that_needs_a_note_below_the_lowest_is_not_offered) {
  // utonal: C at the bottom and G: the subharmonic seventh B𝄫 C E♭ G hangs from G and needs B𝄫 below C.
  ScaleChords scale(Scale::UTONAL, Tuning::EDO53, 0);
  auto low = complete(scale, {0, G}, 4);
  for (auto & chord : low.chords) {
    for (auto note : chord.notes) CHECK(note >= 0);
    CHECK(chord.name.find("G subharmonic seventh") == std::string::npos);
  }
  // An octave up the same chord is offered, with every note one octave higher.
  auto high = complete(scale, {53, 53 + G}, 4);
  CHECK(std::any_of(high.chords.begin(), high.chords.end(), [](const Completion & chord) { return chord.name == "G subharmonic seventh"; }));
  // One played note hangs from an octave above it, so it is never short of room.
  CHECK(!complete(scale, {0}, 4).chords.empty());
}

// The width chooses the class of chord for every note: three columns a triad, four and five a
// seventh chord, six a ninth chord where the note has one and a seventh chord else.
TEST(chords_the_width_chooses_the_class_of_chord) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL, Scale::DIAMOND}) {
    ScaleChords chords(scale, Tuning::EDO53, 0);
    for (int step = 0; step < 53; step++) {
      if (!chords.degreeOf(step)) continue;
      auto at = [&](int width) { return complete(chords, notes({step}), width); };
      auto three = at(3), four = at(4), five = at(5), six = at(6);
      CHECK(!three.chords.empty());
      CHECK(three.chords[0].notes.size() <= 3u);
      CHECK(four.chords[0].notes.size() == 4u);
      // A seventh chord at five columns has its first note an octave on.
      CHECK(five.chords[0].notes.size() == 5u);
      CHECK(five.chords[0].notes[4] == five.chords[0].notes[0] + (five.chords[0].notes[1] > five.chords[0].notes[0] ? 53 : -53));
      CHECK(six.chords[0].notes.size() >= 5u);
      for (auto * r : {&four, &five, &six}) {
        for (auto & chord : r->chords) CHECK(chord.name.find("with octave") == std::string::npos);
      }
      // The fourth column is a seventh chord's, whatever the note.
      auto & name = five.chords[0].name;
      CHECK(name.find("ninth") == std::string::npos);
      CHECK(name.find("seventh") != std::string::npos || name.find("7♯9") != std::string::npos);
    }
  }
}

TEST(chords_names_do_not_depend_on_the_voicing) {
  // 4:5:6:7 from C is a seventh at four and five columns, a ninth only with the run to 9.
  CHECK(completeIn(Scale::OTONAL, {C}, 4).chords[0].name == "C harmonic seventh");
  CHECK(completeIn(Scale::OTONAL, {C}, 5).chords[0].name == "C harmonic seventh");
  CHECK(completeIn(Scale::UTONAL, {C}, 5).chords[0].name == "C subharmonic seventh");
  CHECK(completeIn(Scale::UTONAL, {C}, 6).chords[0].name == "C subharmonic ninth");
  CHECK(completeIn(Scale::UTONAL, {C}, 6).chords[0].notes == notes({53 + C, 36, 22, 10, C, -9}));
  CHECK(completeIn(Scale::UTONAL, {C}, 3).chords[0].name == "F minor");
}

// A seventh chord read from another of its notes repeats the played note an octave on: higher in an
// upward chord, lower in a downward one.
TEST(chords_a_seventh_chord_at_five_columns_repeats_the_first_note) {
  auto d = completeIn(Scale::UTONAL, {8}, 5); // D↓
  CHECK(d.chords[0].name == "B♭↓ harmonic seventh / D↓");
  CHECK(d.chords[0].notes == notes({8, 22, 34, 44, 8 + 53}));
  auto f = completeIn(Scale::UTONAL, {F}, 5);
  CHECK(f.chords[0].notes.size() == 5u);
  CHECK(f.chords[0].notes[4] == f.chords[0].notes[0] - 53);
  CHECK(f.chords[0].name.find("seventh") != std::string::npos);
}

// ---- Runs: the chord of consecutive ratios from the played note ----

namespace {

struct Run {
  std::vector<int> values;
  std::vector<std::size_t> notes;
};

// The run found from the definition: upward the note times 5/4, 6/4, ... 9/4, downward the note an octave up
// times 4/5, 4/6, ... 4/9, stopping at the first ratio the scale does not hold.
Run oracleRun(const ScaleChords & scale, std::size_t low, bool up) {
  auto ratio_of = [](std::size_t i) { return gamut::kExtendedDiamond[i].ratio; };
  const int edo = scale.edo();
  const int base = 5 * edo + scale.stepOf(low) + (up ? 0 : edo);
  Run run;
  run.values.push_back(base);
  run.notes.push_back(low);
  for (int k = 5; k <= 9; k++) {
    auto ratio = (up ? ratio_of(low) * Ratio{k, 4} : ratio_of(low) * Ratio{4, k}).reduced();
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
      if (ratio_of(i) == ratio) found = i;
    }
    if (!found || !scale.holds(*found)) break;
    int offset = up ? scale.stepOf(*found) - scale.stepOf(low) : scale.stepOf(low) - scale.stepOf(*found);
    offset = ((offset % edo) + edo) % edo + (k >= 8 ? edo : 0);
    run.values.push_back(up ? base + offset : base - offset);
    run.notes.push_back(*found);
  }
  return run;
}

bool offers(const Result & result, const Run & run, std::size_t count) {
  std::vector<int> values(run.values.begin(), run.values.begin() + static_cast<std::ptrdiff_t>(std::min(count, run.values.size())));
  return std::any_of(result.chords.begin(), result.chords.end(), [&](const Completion & c) { return c.notes == values; });
}

} // namespace

TEST(runs_every_note_of_the_three_scales_offers_its_runs) {
  int checked = 0;
  for (auto scale : {Scale::OTONAL, Scale::UTONAL, Scale::DIAMOND}) {
    for (bool borrowed : {false, true}) {
      ScaleChords chords(scale, Tuning::EDO53, 0, borrowed);
      for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
        if (!chords.holds(i)) continue;
        auto result = complete(chords, {kC + chords.stepOf(i)}, 6);
        for (bool up : {true, false}) {
          auto run = oracleRun(chords, i, up);
          if (run.values.size() < 3) continue;
          CHECK(offers(result, run, 6));
          checked++;
        }
      }
    }
  }
  CHECK(checked > 40);
}

// A run stops at the first ratio the scale lacks: the remaining columns are left empty.
TEST(runs_stop_where_the_scale_runs_out) {
  // Otonal E: E5 C5 A4, and G♭↓ (1/7 of the way) is no note of otonal.
  auto e = completeIn(Scale::OTONAL, {17}, 6);
  CHECK(std::any_of(e.chords.begin(), e.chords.end(), [](const Completion & c) { return c.notes == notes({53 + 17, 53 + 0, 39}); }));
  for (auto & c : e.chords) {
    if (c.notes.size() > 3 && c.notes[0] == kC + 53 + 17 && c.notes[1] == kC + 53 && c.notes[2] == kC + 39) CHECK(false);
  }
  // Otonal D♯: D♯ F𝄪 A♯↑ D𝄫 D♯, and 9/4 of D♯ is no note of otonal.
  auto ds = completeIn(Scale::OTONAL, {12}, 6);
  CHECK(std::any_of(ds.chords.begin(), ds.chords.end(), [](const Completion & c) { return c.notes.size() == 5 && c.notes[0] == kC + 12 && c.notes[4] == kC + 12 + 53 && c.notes[1] == kC + 29; }));
  // And with the scale's own G♭↓ taken away nothing changes: a run never takes a note the scale lacks.
  CHECK(completeIn(Scale::OTONAL, {17}, 4, Tuning::EDO53).chords[0].notes.size() == 4); // a seventh chord of the list instead
}

// A note that names both runs gives the longer first, and the kind the scale is named for when they are
// equally long.
TEST(runs_the_longer_of_two_comes_first_and_equals_go_by_the_scale) {
  int longer = 0, equal = 0; // longer: none exists in these scales
  for (auto scale : {Scale::OTONAL, Scale::UTONAL, Scale::DIAMOND}) {
    for (auto tuning : {Tuning::EDO53, Tuning::EDO31, Tuning::EDO19}) {
      ScaleChords chords(scale, tuning, 0, tuning == Tuning::EDO53);
      const bool preferred_up = scale != Scale::UTONAL;
      for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
        if (!chords.holds(i)) continue;
        auto up = oracleRun(chords, i, true), down = oracleRun(chords, i, false);
        if (std::max(up.values.size(), down.values.size()) < 3) continue;
        bool first_up = up.values.size() == down.values.size() ? preferred_up : up.values.size() > down.values.size();
        auto & want = first_up ? up : down;
        auto result = complete(chords, {5 * chords.edo() + chords.stepOf(i)}, 3);
        CHECK(!result.chords.empty());
        CHECK(result.chords[0].notes == std::vector<int>(want.values.begin(), want.values.begin() + 3));
        if (up.values.size() >= 3 && down.values.size() >= 3) (up.values.size() == down.values.size() ? equal : longer)++;
      }
    }
  }
  // The only notes with two runs are equal ones, in the diamond; no note of these scales has runs of
  // different lengths, so the longer-first rule is only checked where it cannot yet differ.
  CHECK(equal > 0);
}

// The minor triad is upward from its lowest note and the downward run from its top note.
TEST(runs_the_minor_triad_from_both_ends) {
  // Otonal: A C E from A, and E C A from E (the run).
  auto from_a = completeIn(Scale::OTONAL, {39}, 3);
  CHECK(std::any_of(from_a.chords.begin(), from_a.chords.end(), [](const Completion & c) { return c.name == "A minor" && c.notes == notes({39, 53, 53 + 17}); }));
  auto from_e = completeIn(Scale::OTONAL, {17}, 3);
  CHECK(from_e.chords[0].name == "A minor");
  CHECK(from_e.chords[0].notes == notes({53 + 17, 53, 39}));
  // Utonal: F A♭ C from F, and C A♭ F from C.
  auto from_f = completeIn(Scale::UTONAL, {22}, 3);
  CHECK(std::any_of(from_f.chords.begin(), from_f.chords.end(), [](const Completion & c) { return c.name == "F minor" && c.notes == notes({22, 36, 53}); }));
  auto from_c = completeIn(Scale::UTONAL, {0}, 3);
  CHECK(from_c.chords[0].name == "F minor");
  CHECK(from_c.chords[0].notes == notes({53, 36, 22}));
}

// In otonal C's list the run comes first, then the other seventh chords C belongs to, then C major seventh and C 7♯9.
TEST(runs_only_the_run_is_moved_to_the_front_of_a_list) {
  auto c = completeIn(Scale::OTONAL, {0}, 4);
  CHECK(c.chords[0].name == "C harmonic seventh");
  CHECK(c.chords[1].name == "F harmonic seventh / C");
  auto position = [&](const std::string & name) {
    for (std::size_t i = 0; i < c.chords.size(); i++) {
      if (c.chords[i].name == name) return static_cast<int>(i);
    }
    return -1;
  };
  CHECK(position("F harmonic seventh / C") < position("C major seventh"));
  CHECK(position("F harmonic seventh / C") < position("C 7♯9"));
}
