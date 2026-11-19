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
    {"C", {"C 7♯9 with fifth", "C harmonic ninth", "C major ninth", "F harmonic ninth / C", "F major ninth / C", "C mirror harmonic ninth", "A mirror major ninth / C", "C mirror 7♯9 with fifth"}},
    {"D♭↓", {"A mirror harmonic ninth / D♭↓"}},
    {"D", {"C harmonic ninth / D", "C mirror harmonic ninth / D", "C major ninth / D"}},
    {"D♯", {"F harmonic ninth / D♯", "C 7♯9 with fifth / D♯", "C mirror 7♯9 with fifth / D♯"}},
    {"E", {"F major ninth / E", "C 7♯9 with fifth / E", "C harmonic ninth / E", "A mirror harmonic ninth / E", "C major ninth / E", "A mirror major ninth / E"}},
    {"F♭", {"C mirror harmonic ninth / F♭"}},
    {"F", {"F harmonic ninth", "F major ninth"}},
    {"F♯↑", {"C mirror 7♯9 with fifth / F♯↑"}},
    {"G", {"A mirror harmonic ninth / G", "A mirror major ninth / G", "F harmonic ninth / G", "F major ninth / G", "C 7♯9 with fifth / G", "C harmonic ninth / G", "C mirror harmonic ninth / G", "C major ninth / G", "C mirror 7♯9 with fifth / G"}},
    {"A", {"F harmonic ninth / A", "F major ninth / A", "A mirror harmonic ninth", "A mirror major ninth"}},
    {"A♯↑", {"C mirror 7♯9 with fifth / A♯↑", "C 7♯9 with fifth / A♯↑", "C harmonic ninth / A♯↑"}},
    {"B♭", {"C mirror harmonic ninth / B♭"}},
    {"B", {"A mirror harmonic ninth / B", "C major ninth / B", "A mirror major ninth / B"}},
};

const NinthNames kUtonalNinthNames = {
    {"C", {"E𝄫 mirror 7♯9 with fifth / C", "F mirror harmonic ninth / C", "F mirror major ninth / C", "B♭↓ harmonic ninth / C", "B♭↓ mirror harmonic ninth / C", "D♭↓ major ninth / C", "B♭↓ mirror major ninth / C", "E𝄫 7♯9 with fifth / C"}},
    {"D♭↓", {"D♭↓ harmonic ninth", "D♭↓ major ninth", "B♭↓ mirror major ninth / D♭↓"}},
    {"D↓", {"B♭↓ harmonic ninth / D↓"}},
    {"E𝄫", {"E𝄫 7♯9 with fifth", "E𝄫 mirror 7♯9 with fifth", "B♭↓ mirror harmonic ninth / E𝄫"}},
    {"E♭", {"F mirror harmonic ninth / E♭", "F mirror major ninth / E♭", "D♭↓ harmonic ninth / E♭", "D♭↓ major ninth / E♭"}},
    {"F", {"F mirror harmonic ninth", "F mirror major ninth", "E𝄫 mirror 7♯9 with fifth / F", "D♭↓ harmonic ninth / F", "B♭↓ harmonic ninth / F", "B♭↓ mirror harmonic ninth / F", "D♭↓ major ninth / F", "B♭↓ mirror major ninth / F", "E𝄫 7♯9 with fifth / F"}},
    {"G♭↓", {"E𝄫 7♯9 with fifth / G♭↓"}},
    {"G", {"F mirror harmonic ninth / G", "F mirror major ninth / G"}},
    {"G♯", {"B♭↓ harmonic ninth / G♯"}},
    {"A♭", {"B♭↓ mirror harmonic ninth / A♭", "B♭↓ mirror major ninth / A♭", "F mirror major ninth / A♭", "E𝄫 mirror 7♯9 with fifth / A♭", "D♭↓ harmonic ninth / A♭", "D♭↓ major ninth / A♭"}},
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

TEST(chords_are_named_after_their_root) {
  // A harmonic seventh by its lowest note, a mirror seventh by its top note. A note's list starts
  // with the chords it names (its seventh chords, then its triads, then the others).
  using V = std::vector<std::string>;
  CHECK(namesOf(Scale::OTONAL, "C") == (V{"C harmonic seventh", "C major seventh", "C 7♯9", "F harmonic seventh / C", "A♯↑ mirror seventh / C", "F major seventh / C"}));
  CHECK(namesOf(Scale::OTONAL, "D♯") == (V{"D♯ harmonic seventh", "F harmonic seventh / D♯", "A♯↑ mirror seventh / D♯", "C 7♯9 / D♯"}));
  CHECK(namesOf(Scale::OTONAL, "A") == (V{"F harmonic seventh / A", "A minor", "F major seventh / A"}));
  CHECK(namesOf(Scale::OTONAL, "D𝄪") == (V{"D𝄪 mirror seventh", "F♯↑ 7♯9 / D𝄪"}));
  CHECK(namesOf(Scale::OTONAL, "D♭↓") == (V{"B mirror seventh / D♭↓"}));
  CHECK(namesOf(Scale::OTONAL, "B") == (V{"B mirror seventh", "C major seventh / B"}));
  CHECK(namesOf(Scale::UTONAL, "C") == (V{"C mirror seventh", "G mirror seventh / C", "E𝄫 harmonic seventh / C", "A♭ major seventh / C", "D♭↓ major seventh / C", "E𝄫 7♯9 / C"}));
  CHECK(namesOf(Scale::UTONAL, "A♭") == (V{"A♭ major", "A♭ major seventh", "C mirror seventh / A♭", "D♭↓ harmonic seventh / A♭", "D♭↓ major seventh / A♭", "G𝄫 7♯9 / A♭"}));
  CHECK(namesOf(Scale::UTONAL, "D♭↓") == (V{"D♭↓ harmonic seventh", "D♭↓ major seventh"}));
  CHECK(namesOf(Scale::UTONAL, "B♯") == (V{"B♯ 7♯9", "B𝄫 mirror seventh / B♯"}));
  CHECK(namesOf(Scale::UTONAL, "B") == (V{"D♭↓ harmonic seventh / B"}));
  CHECK(namesOf(Scale::UTONAL, "E𝄫") == (V{"E𝄫 harmonic seventh", "E𝄫 7♯9", "C mirror seventh / E𝄫", "B𝄫 mirror seventh / E𝄫", "B♯ 7♯9 / E𝄫"}));
}

TEST(chords_one_note_is_completed_to_the_first_notes_of_its_first_chord) {
  auto four = completeIn(Scale::OTONAL, {C}, 4);
  CHECK(four.status == Status::OK);
  CHECK(four.chords[0].notes == notes({C, E, G, Asu}));
  CHECK(four.chords[0].name == "C harmonic seventh");
  CHECK(four.chords.size() == 6);
  // The chords C names come first: its harmonic seventh, its major seventh, its 7♯9.
  CHECK(four.chords[1].name == "C major seventh");
  CHECK(four.chords[2].name == "C 7♯9");

  auto three = completeIn(Scale::OTONAL, {C}, 3);
  CHECK(three.chords[0].notes == notes({C, E, G}));
  CHECK(three.chords[0].name == "C major");
  // The alternatives in list order; C E G of the major seventh repeats the first and is dropped.
  CHECK(three.chords.size() == 5);
  CHECK(three.chords[1].notes == notes({C, Ds, E}));
  CHECK(three.chords[1].name == "C 7♯9 (3 notes)");
  CHECK(three.chords[2].notes == notes({C, Ds, F}));
  CHECK(three.chords[2].name == "C narrow major");
  // A mirror seventh hangs from the played note's octave up, so its three notes at a width of
  // three are the top three: F♯↑ A♯↑ C, ending an octave above the played C.
  CHECK(three.chords[3].notes == notes({Fsu, Asu, 53 + C}));
  CHECK(three.chords[3].name == "A♯↑ mirror seventh / C (3 notes)");
  CHECK(three.chords[4].notes == notes({C, E, F}));
  CHECK(three.chords[4].name == "F major seventh / C (3 notes)");

  auto two = completeIn(Scale::OTONAL, {C}, 2);
  CHECK(two.chords.size() == 3);
  CHECK(two.chords[0].notes == notes({C, E}));
  CHECK(two.chords[0].name == "C harmonic seventh (2 notes)");
  CHECK(two.chords[1].notes == notes({C, Ds}));
  CHECK(two.chords[2].notes == notes({Asu, 53 + C})); // the mirror seventh, from its top note

  auto a = completeIn(Scale::OTONAL, {A}, 3);
  CHECK(a.chords[0].notes == notes({A, 53 + C, 53 + Ds}));
  CHECK(a.chords[0].name == "A septimal diminished");
  // The minor triad hangs from its top note: A C E under an A an octave above the played one.
  CHECK(a.chords[1].notes == notes({53 + C, 53 + E, 53 + A}));
  CHECK(a.chords[1].name == "A minor");
  CHECK(a.chords[2].notes == notes({A, 53 + C, 53 + E})); // the cut of the major seventh F A C E, upward from A

  auto utonal = completeIn(Scale::UTONAL, {C}, 4);
  CHECK(utonal.chords[0].notes == notes({Ebb, F, Ab, 53 + C}));
  CHECK(utonal.chords[0].name == "C mirror seventh");
  auto utonal_three = completeIn(Scale::UTONAL, {C}, 3);
  CHECK(utonal_three.chords[0].name == "F minor"); // F A♭ C, from its top note C
  CHECK(std::any_of(utonal_three.chords.begin(), utonal_three.chords.end(), [](const Completion & c) { return c.name == "C narrow minor"; }));
  CHECK(completeIn(Scale::UTONAL, {Ab}, 3).chords[0].name == "A♭ major"); // the triad A♭ names comes first
}

TEST(chords_several_given_notes_pick_the_chords_that_contain_them) {
  auto c_ds = completeIn(Scale::OTONAL, {C, Ds}, 4);
  CHECK(c_ds.chords.size() == 3);
  CHECK(c_ds.chords[0].notes == notes({C, Ds, E, Asu}));
  CHECK(c_ds.chords[0].name == "C 7♯9");
  CHECK(c_ds.chords[1].notes == notes({C, Ds, F, A}));
  CHECK(c_ds.chords[1].name == "F harmonic seventh / C");
  // The mirror seventh is found from the highest given note, D♯, and hangs from it: the played
  // notes keep their octaves and nothing is added above D♯.
  CHECK(c_ds.chords[2].notes == notes({Fsu - 53, Asu - 53, C, Ds}));
  CHECK(c_ds.chords[2].name == "A♯↑ mirror seventh / D♯");

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
  CHECK(minor.chords[0].notes == notes({A, 53 + C, 53 + E, 53 + F}));
  CHECK(minor.chords[1].notes == notes({A, 53 + C, 53 + E}));
  CHECK(minor.chords[1].name == "A minor");
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
  // The five-note chords C names come first: its 7♯9 with fifth.
  CHECK(c.chords[0].notes == notes({C, Ds, E, G, Asu}));
  CHECK(c.chords[0].name == "C 7♯9 with fifth");
  CHECK(c.chords[1].name == "C harmonic ninth");

  auto fsu = completeIn(Scale::OTONAL, {Fsu}, 5);
  CHECK(fsu.chords[0].notes == notes({G, Asu, 53 + C, 53 + Ds, 53 + Fsu})); // a mirror chord: hung from an octave above the played F♯↑
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
  // The chords C names (as their top note) come first: a mirror chord hung from an octave above the played C.
  CHECK(c.chords[0].notes == notes({Ebb, F, Ab, Bbb, 53 + C}));
  CHECK(c.chords[0].name == "E𝄫 mirror 7♯9 with fifth / C");
  CHECK(c.chords[1].notes == notes({Eb, F, G, Bbb, 53 + C}));
  CHECK(c.chords[1].name == "F mirror harmonic ninth / C");
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

// ---- Mirror chords are built downward from the played note ----

TEST(every_kind_of_chord_has_a_direction_and_the_mirror_of_an_upward_kind_is_downward) {
  const std::vector<std::pair<Kind, Direction>> kinds = {
      {Kind::HARMONIC_SEVENTH, Direction::UPWARD},
      {Kind::MIRROR_SEVENTH, Direction::DOWNWARD},
      {Kind::MAJOR_SEVENTH, Direction::UPWARD},
      {Kind::SEVEN_SHARP_NINE, Direction::UPWARD},
      {Kind::MAJOR, Direction::UPWARD},
      {Kind::MINOR, Direction::DOWNWARD},
      {Kind::HARMONIC_NINTH, Direction::UPWARD},
      {Kind::MIRROR_HARMONIC_NINTH, Direction::DOWNWARD},
      {Kind::MAJOR_NINTH, Direction::UPWARD},
      {Kind::MIRROR_MAJOR_NINTH, Direction::DOWNWARD},
      {Kind::SEVEN_SHARP_NINE_FIFTH, Direction::UPWARD},
      {Kind::MIRROR_SEVEN_SHARP_NINE_FIFTH, Direction::DOWNWARD},
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

// A mirror chord is written with its top note an octave above the played note, and nothing above that.
TEST(a_mirror_chord_is_written_downward_from_the_played_note) {
  // utonal C: E𝄫4 F4 A♭4 C5
  auto c = completeIn(Scale::UTONAL, {C}, 4);
  CHECK(relative(c.chords[0]) == (std::vector<int>{Ebb, F, Ab, 53 + C}));
  // utonal G: B𝄫4 C5 E♭5 G5
  auto g = completeIn(Scale::UTONAL, {G}, 4);
  CHECK(relative(g.chords[0]) == (std::vector<int>{Bbb, 53 + C, 53 + Eb, 53 + G}));
  CHECK(c.chords[0].notes.back() == kC + 53 + C); // nothing above the top note
  // otonal C: unchanged, C4 E4 G4 A♯↑4.
  CHECK(relative(completeIn(Scale::OTONAL, {C}, 4).chords[0]) == (std::vector<int>{C, E, G, Asu}));
  // An upward chord of utonal is as before: E𝄫4 G♭↓4 B𝄫4 C5 is one of E𝄫's candidates.
  auto ebb = completeIn(Scale::UTONAL, {Ebb}, 4);
  CHECK(std::any_of(ebb.chords.begin(), ebb.chords.end(), [](const Completion & chord) {
    return relative(chord) == (std::vector<int>{Ebb, Gbv, Bbb, 53 + C}) && chord.name == "E𝄫 harmonic seventh";
  }));
}

// A chord with more notes than the cursor is wide is cut from its far end: a mirror chord keeps the played
// note and the notes just below it.
TEST(a_mirror_chord_is_cut_to_the_width_from_the_bottom) {
  auto three = completeIn(Scale::UTONAL, {C}, 3);
  CHECK(relative(three.chords[0]) == (std::vector<int>{F, Ab, 53 + C}));
  auto two = completeIn(Scale::UTONAL, {C}, 2);
  CHECK(relative(two.chords[0]) == (std::vector<int>{Ab, 53 + C}));
  // An upward chord keeps the played note and the notes just above it, as before.
  CHECK(relative(completeIn(Scale::OTONAL, {C}, 3).chords[0]) == (std::vector<int>{C, E, G}));
}

// A downward chord in a note's list is read downward from the note, even where the note is not its top note.
TEST(a_downward_chord_in_the_list_is_read_downward_from_the_note) {
  // The minor triad A C E: under a played A, A is the top.
  auto a = completeIn(Scale::OTONAL, {A}, 3);
  CHECK(std::any_of(a.chords.begin(), a.chords.end(), [](const Completion & chord) { return relative(chord) == (std::vector<int>{53 + C, 53 + E, 53 + A}) && chord.name == "A minor"; }));
}

// ---- Phase 2: several played notes, the order of the lists, the range limit ----

// With one played note, the chords the note names come first, in its scale: a mirror chord hangs from an
// octave above the played note.
TEST(a_note_gives_the_chord_it_is_heard_as) {
  // otonal A♯↑ names the mirror seventh C D♯ F♯↑ A♯↑: C5 D♯5 F♯↑5 A♯↑5
  CHECK(relative(completeIn(Scale::OTONAL, {Asu}, 4).chords[0]) == (std::vector<int>{53 + C, 53 + Ds, 53 + Fsu, 53 + Asu}));
  // otonal B names D♭↓ E G B: D♭↓5 E5 G5 B5
  CHECK(relative(completeIn(Scale::OTONAL, {B}, 4).chords[0]) == (std::vector<int>{53 + Dbv, 53 + E, 53 + G, 53 + B}));
  // otonal D names F♭ G B♭ D: F♭4 G4 B♭4 D5
  CHECK(relative(completeIn(Scale::OTONAL, {9}, 4).chords[0]) == (std::vector<int>{19, G, 45, 53 + 9}));
  // otonal E names no chord: its first chord is as it was, C harmonic seventh / E
  auto e = completeIn(Scale::OTONAL, {E}, 4);
  CHECK(relative(e.chords[0]) == (std::vector<int>{E, G, Asu, 53 + C}));
  CHECK(e.chords[0].name == "C harmonic seventh / E");
  // diamond E names the mirror seventh G♭↓ A C E: G♭↓4 A4 C5 E5, and cut to three notes A4 C5 E5
  CHECK(relative(completeIn(Scale::DIAMOND, {E}, 4).chords[0]) == (std::vector<int>{Gbv, A, 53 + C, 53 + E}));
  CHECK(relative(completeIn(Scale::DIAMOND, {E}, 3).chords[0]) == (std::vector<int>{A, 53 + C, 53 + E}));
}

// A note that names both an upward and a downward seventh chord gives first the kind the scale is named for.
TEST(the_kind_the_scale_is_named_for_comes_first_where_a_note_names_both) {
  auto first_two = [](Scale scale) {
    ScaleChords chords(scale, Tuning::EDO53, 0);
    std::vector<std::string> out;
    auto list = chords.chordsOf(0);
    for (std::size_t i = 0; i < 2 && i < list.size(); i++) out.push_back(chords.chordName(*list[i]));
    return out;
  };
  using V = std::vector<std::string>;
  // The diamond has C as the root of both C harmonic seventh and C mirror seventh (of utonal).
  CHECK(first_two(Scale::DIAMOND) == (V{"C harmonic seventh", "C mirror seventh"}));
  CHECK(first_two(Scale::UTONAL)[0] == "C mirror seventh");
  CHECK(first_two(Scale::OTONAL)[0] == "C harmonic seventh");
}

// Several played notes: the upward chord is found from the lowest, the downward chord from the highest, and each
// contains every played note.
TEST(several_played_notes_find_upward_chords_from_the_lowest_and_downward_chords_from_the_highest) {
  // utonal: F and the C above it write E𝄫 F A♭ C with that C on top; F and C keep their octaves.
  auto f_c = completeIn(Scale::UTONAL, {F, 53 + C}, 4);
  CHECK(f_c.status == Status::OK);
  CHECK(relative(f_c.chords[0]) == (std::vector<int>{Ebb, F, Ab, 53 + C}));
  CHECK(f_c.chords[0].name == "C mirror seventh");
  // Nothing is added above the highest played note.
  for (auto & chord : f_c.chords) {
    for (auto note : chord.notes) CHECK(note <= kC + 53 + C || chord.notes.back() != kC + 53 + C);
  }
  // otonal: C and D♯ give upward chords from C, then the mirror seventh from D♯, in which every note but
  // the played ones is below D♯.
  auto c_ds = completeIn(Scale::OTONAL, {C, Ds}, 4);
  for (auto & chord : c_ds.chords) {
    for (auto played : {kC + C, kC + Ds}) CHECK(std::find(chord.notes.begin(), chord.notes.end(), played) != chord.notes.end());
  }
  bool hangs = false;
  for (auto & chord : c_ds.chords) {
    if (chord.name == "A♯↑ mirror seventh / D♯") {
      hangs = true;
      CHECK(relative(chord) == (std::vector<int>{Fsu - 53, Asu - 53, C, Ds})); // D♯ is the top
    }
  }
  CHECK(hangs);
  // The chord must contain every played note: the notes C, G and A are in no chord together.
  CHECK(completeIn(Scale::OTONAL, {C, G, A}, 4).status == Status::NO_CHORD);
}

// A downward chord that would need a note below the lowest the track can hold is not offered.
TEST(a_downward_chord_that_needs_a_note_below_the_lowest_is_not_offered) {
  // utonal: C at the bottom and G: the mirror seventh B𝄫 C E♭ G hangs from G and needs B𝄫 below C.
  ScaleChords scale(Scale::UTONAL, Tuning::EDO53, 0);
  auto low = complete(scale, {0, G}, 4);
  for (auto & chord : low.chords) {
    for (auto note : chord.notes) CHECK(note >= 0);
    CHECK(chord.name.find("G mirror seventh") == std::string::npos);
  }
  // An octave up the same chord is offered, with every note one octave higher.
  auto high = complete(scale, {53, 53 + G}, 4);
  CHECK(std::any_of(high.chords.begin(), high.chords.end(), [](const Completion & chord) { return chord.name == "G mirror seventh"; }));
  // One played note hangs from an octave above it, so it is never short of room.
  CHECK(!complete(scale, {0}, 4).chords.empty());
}
