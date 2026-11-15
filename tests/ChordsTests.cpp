#include "TestFramework.h"

#include "../src/model/Chords.h"
#include "../src/model/Note.h"

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
    {"C", {"F harmonic ninth / C", "F major ninth / C", "C 7♯9 with fifth", "C harmonic ninth", "C mirror harmonic ninth", "C major ninth", "A mirror major ninth / C", "C mirror 7♯9 with fifth"}},
    {"D♭↓", {"A mirror harmonic ninth / D♭↓"}},
    {"D", {"C harmonic ninth / D", "C mirror harmonic ninth / D", "C major ninth / D"}},
    {"D♯", {"F harmonic ninth / D♯", "C 7♯9 with fifth / D♯", "C mirror 7♯9 with fifth / D♯"}},
    {"E", {"F major ninth / E", "C 7♯9 with fifth / E", "C harmonic ninth / E", "A mirror harmonic ninth / E", "C major ninth / E", "A mirror major ninth / E"}},
    {"F♭", {"C mirror harmonic ninth / F♭"}},
    {"F", {"F harmonic ninth", "F major ninth"}},
    {"F♯↑", {"C mirror 7♯9 with fifth / F♯↑"}},
    {"G", {"F harmonic ninth / G", "F major ninth / G", "C 7♯9 with fifth / G", "C harmonic ninth / G", "A mirror harmonic ninth / G", "C mirror harmonic ninth / G", "C major ninth / G", "A mirror major ninth / G", "C mirror 7♯9 with fifth / G"}},
    {"A", {"F harmonic ninth / A", "F major ninth / A", "A mirror harmonic ninth", "A mirror major ninth"}},
    {"A♯↑", {"C 7♯9 with fifth / A♯↑", "C harmonic ninth / A♯↑", "C mirror 7♯9 with fifth / A♯↑"}},
    {"B♭", {"C mirror harmonic ninth / B♭"}},
    {"B", {"A mirror harmonic ninth / B", "C major ninth / B", "A mirror major ninth / B"}},
};

const NinthNames kUtonalNinthNames = {
    {"C", {"F mirror harmonic ninth / C", "F mirror major ninth / C", "E𝄫 mirror 7♯9 with fifth / C", "B♭↓ harmonic ninth / C", "B♭↓ mirror harmonic ninth / C", "D♭↓ major ninth / C", "B♭↓ mirror major ninth / C", "E𝄫 7♯9 with fifth / C"}},
    {"D♭↓", {"D♭↓ harmonic ninth", "D♭↓ major ninth", "B♭↓ mirror major ninth / D♭↓"}},
    {"D↓", {"B♭↓ harmonic ninth / D↓"}},
    {"E𝄫", {"E𝄫 mirror 7♯9 with fifth", "B♭↓ mirror harmonic ninth / E𝄫", "E𝄫 7♯9 with fifth"}},
    {"E♭", {"F mirror harmonic ninth / E♭", "F mirror major ninth / E♭", "D♭↓ harmonic ninth / E♭", "D♭↓ major ninth / E♭"}},
    {"F", {"F mirror harmonic ninth", "F mirror major ninth", "E𝄫 mirror 7♯9 with fifth / F", "D♭↓ harmonic ninth / F", "B♭↓ harmonic ninth / F", "B♭↓ mirror harmonic ninth / F", "D♭↓ major ninth / F", "B♭↓ mirror major ninth / F", "E𝄫 7♯9 with fifth / F"}},
    {"G♭↓", {"E𝄫 7♯9 with fifth / G♭↓"}},
    {"G", {"F mirror harmonic ninth / G", "F mirror major ninth / G"}},
    {"G♯", {"B♭↓ harmonic ninth / G♯"}},
    {"A♭", {"F mirror major ninth / A♭", "E𝄫 mirror 7♯9 with fifth / A♭", "D♭↓ harmonic ninth / A♭", "B♭↓ mirror harmonic ninth / A♭", "D♭↓ major ninth / A♭", "B♭↓ mirror major ninth / A♭"}},
    {"B𝄫", {"F mirror harmonic ninth / B𝄫", "E𝄫 mirror 7♯9 with fifth / B𝄫", "E𝄫 7♯9 with fifth / B𝄫"}},
    {"B♭↓", {"B♭↓ harmonic ninth", "B♭↓ mirror harmonic ninth", "B♭↓ mirror major ninth"}},
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

TEST(chords_are_named_after_the_lowest_note_of_their_form) {
  using V = std::vector<std::string>;
  CHECK(namesOf(Scale::OTONAL, "C") == (V{"C harmonic seventh", "F harmonic seventh / C", "C mirror seventh", "F major seventh / C",
                                          "C major seventh", "C 7♯9"}));
  CHECK(namesOf(Scale::OTONAL, "D♯") == (V{"F harmonic seventh / D♯", "C mirror seventh / D♯", "D♯ harmonic seventh", "C 7♯9 / D♯"}));
  CHECK(namesOf(Scale::OTONAL, "A") == (V{"F harmonic seventh / A", "A minor", "F major seventh / A"}));
  CHECK(namesOf(Scale::OTONAL, "D𝄪") == (V{"F♯↑ mirror seventh / D𝄪", "F♯↑ 7♯9 / D𝄪"}));
  CHECK(namesOf(Scale::OTONAL, "D♭↓") == (V{"D♭↓ mirror seventh"}));
  CHECK(namesOf(Scale::OTONAL, "B") == (V{"D♭↓ mirror seventh / B", "C major seventh / B"}));
  CHECK(namesOf(Scale::UTONAL, "C") == (V{"E𝄫 mirror seventh / C", "B𝄫 mirror seventh / C", "E𝄫 harmonic seventh / C",
                                          "A♭ major seventh / C", "D♭↓ major seventh / C", "E𝄫 7♯9 / C"}));
  CHECK(namesOf(Scale::UTONAL, "A♭") == (V{"E𝄫 mirror seventh / A♭", "D♭↓ harmonic seventh / A♭", "A♭ major", "A♭ major seventh",
                                           "D♭↓ major seventh / A♭", "G𝄫 7♯9 / A♭"}));
  CHECK(namesOf(Scale::UTONAL, "D♭↓") == (V{"D♭↓ harmonic seventh", "D♭↓ major seventh"}));
  CHECK(namesOf(Scale::UTONAL, "B♯") == (V{"B♯ mirror seventh", "B♯ 7♯9"}));
  CHECK(namesOf(Scale::UTONAL, "B") == (V{"D♭↓ harmonic seventh / B"}));
}

TEST(chords_one_note_is_completed_to_the_first_notes_of_its_first_chord) {
  auto four = completeIn(Scale::OTONAL, {C}, 4);
  CHECK(four.status == Status::OK);
  CHECK(four.chords[0].notes == notes({C, E, G, Asu}));
  CHECK(four.chords[0].name == "C harmonic seventh");
  CHECK(four.chords.size() == 6);

  auto three = completeIn(Scale::OTONAL, {C}, 3);
  CHECK(three.chords[0].notes == notes({C, E, G}));
  CHECK(three.chords[0].name == "C major");
  // The alternatives in list order; C E G of the major seventh repeats the first and is dropped.
  CHECK(three.chords.size() == 5);
  CHECK(three.chords[1].notes == notes({C, Ds, F}));
  CHECK(three.chords[1].name == "C narrow major");
  CHECK(three.chords[2].notes == notes({C, Ds, Fsu}));
  CHECK(three.chords[2].name == "C mirror diminished");
  CHECK(three.chords[3].notes == notes({C, E, F}));
  CHECK(three.chords[3].name == "F major seventh / C (3 notes)");
  CHECK(three.chords[4].notes == notes({C, Ds, E}));

  auto two = completeIn(Scale::OTONAL, {C}, 2);
  CHECK(two.chords.size() == 2);
  CHECK(two.chords[0].notes == notes({C, E}));
  CHECK(two.chords[0].name == "C harmonic seventh (2 notes)");
  CHECK(two.chords[1].notes == notes({C, Ds}));

  auto a = completeIn(Scale::OTONAL, {A}, 3);
  CHECK(a.chords[0].notes == notes({A, 53 + C, 53 + Ds}));
  CHECK(a.chords[0].name == "A septimal diminished");
  CHECK(a.chords[1].notes == notes({A, 53 + C, 53 + E}));
  CHECK(a.chords[1].name == "A minor");

  auto utonal = completeIn(Scale::UTONAL, {C}, 4);
  CHECK(utonal.chords[0].notes == notes({C, Ebb, F, Ab}));
  CHECK(utonal.chords[0].name == "E𝄫 mirror seventh / C");
  CHECK(completeIn(Scale::UTONAL, {C}, 3).chords[0].name == "C narrow minor");
  CHECK(completeIn(Scale::UTONAL, {Ab}, 3).chords[2].name == "A♭ major");
}

TEST(chords_several_given_notes_pick_the_chords_that_contain_them) {
  auto c_ds = completeIn(Scale::OTONAL, {C, Ds}, 4);
  CHECK(c_ds.chords.size() == 3);
  CHECK(c_ds.chords[0].notes == notes({C, Ds, F, A}));
  CHECK(c_ds.chords[0].name == "F harmonic seventh / C");
  CHECK(c_ds.chords[1].notes == notes({C, Ds, Fsu, Asu}));
  CHECK(c_ds.chords[2].notes == notes({C, Ds, E, Asu}));

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
  CHECK(triadName(Ratio{7, 6}, Ratio{7, 5}) == "mirror diminished");
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

TEST(chords_five_notes_are_completed_first_at_a_width_of_five) {
  auto f = completeIn(Scale::OTONAL, {F}, 5);
  CHECK(f.chords[0].notes == notes({F, G, A, 53 + C, 53 + Ds}));
  CHECK(f.chords[0].name == "F harmonic ninth");
  CHECK(f.chords[1].notes == notes({F, G, A, 53 + C, 53 + E}));
  CHECK(f.chords[1].name == "F major ninth");
  // The four-note chords follow, as they are found at four.
  auto four = completeIn(Scale::OTONAL, {F}, 4);
  CHECK(f.chords.size() == 2 + four.chords.size());
  for (std::size_t i = 0; i < four.chords.size(); i++) {
    CHECK(f.chords[2 + i].notes == four.chords[i].notes);
    CHECK(f.chords[2 + i].name == four.chords[i].name);
  }

  auto c = completeIn(Scale::OTONAL, {C}, 5);
  CHECK(c.chords[0].notes == notes({C, Ds, F, G, A}));
  CHECK(c.chords[0].name == "F harmonic ninth / C");

  auto fsu = completeIn(Scale::OTONAL, {Fsu}, 5);
  CHECK(fsu.chords[0].notes == notes({Fsu, G, Asu, 53 + C, 53 + Ds}));
  CHECK(fsu.chords[0].name == "C mirror 7♯9 with fifth / F♯↑");

  CHECK(completeIn(Scale::OTONAL, {C, B}, 5).chords[0].notes == notes({C, 9, E, G, B}));
  CHECK(completeIn(Scale::OTONAL, {C, B}, 5).chords[0].name == "C major ninth");
}

TEST(chords_a_degree_without_five_note_chords_falls_back_to_four) {
  // D𝄫 (step 2 in 53-EDO) has none.
  auto dbb = completeIn(Scale::OTONAL, {Dbb}, 5);
  CHECK(dbb.status == Status::OK);
  auto four = completeIn(Scale::OTONAL, {Dbb}, 4);
  CHECK(dbb.chords.size() == four.chords.size());
  for (auto & chord : dbb.chords) CHECK(chord.notes.size() == 4);
  CHECK(dbb.chords[0].notes == four.chords[0].notes);
}

TEST(chords_five_note_chords_are_not_offered_at_narrower_widths) {
  for (int size : {2, 3, 4}) {
    for (auto & chord : completeIn(Scale::OTONAL, {C}, size).chords) CHECK(static_cast<int>(chord.notes.size()) <= size);
    for (auto & chord : completeIn(Scale::OTONAL, {F}, size).chords) CHECK(chord.name.find("ninth") == std::string::npos);
  }
  // A width beyond the largest chord is the largest.
  CHECK(completeIn(Scale::OTONAL, {F}, 7).chords[0].name == "F harmonic ninth");
  // As many given notes as the width: nothing to add.
  CHECK(completeIn(Scale::OTONAL, {C, Ds, F, G, A}, 5).status == Status::NOTHING_TO_ADD);
  // Four given notes of a five-note chord are completed by it.
  auto four = completeIn(Scale::OTONAL, {C, Ds, F, G}, 5);
  CHECK(four.status == Status::OK);
  CHECK(four.chords[0].notes == notes({C, Ds, F, G, A}));
}

TEST(chords_utonal_five_note_chords_complete_from_a_degree) {
  auto c = completeIn(Scale::UTONAL, {C}, 5);
  CHECK(c.chords[0].notes == notes({C, Eb, F, G, Bbb}));
  CHECK(c.chords[0].name == "F mirror harmonic ninth / C");
  CHECK(c.chords[1].name == "F mirror major ninth / C");
}
