#include "TestFramework.h"

#include "../src/model/ArrangementOps.h"
#include "../src/model/Chords.h"
#include "../src/model/Clip.h"
#include "../src/model/ClipView.h"
#include "../src/model/JustIntonation.h"
#include "../src/model/Note.h"
#include "../src/model/PatternBlockOps.h"
#include "../src/model/PatternGrid.h"
#include "../src/model/Song.h"
#include "../src/playback/KeyboardNotes.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace chords;
using namespace ratio_literals;
using NoteRatio = just_intonation::ScaleCorrections::NoteRatio;

namespace {

const int kC = 5 * 53; // C-4 in 53-EDO
const int kA = 39;

// A key's note, which the test says the key gives.
KeyboardNote noteOf(int id, Scale scale, int song_key = 0, Tuning tuning = Tuning::EDO53) {
  auto note = keyToNote(id, tuning, scale, song_key, 4);
  CHECK(note.has_value());
  return note.value_or(KeyboardNote{});
}

std::size_t index(Ratio ratio) {
  for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
    if (gamut::kExtendedDiamond[i].ratio == ratio) return i;
  }
  CHECK(false);
  return 0;
}

double correctionOf(Ratio ratio, int edo = 53) {
  return ratio.cents() - ratio.steps(edo) * 1200.0 / edo;
}

std::set<std::size_t> setOf(std::initializer_list<Ratio> ratios) {
  std::set<std::size_t> out;
  for (auto ratio : ratios) out.insert(index(ratio));
  return out;
}

// The keys of a tuning a scale's notes lie on, ascending.
std::vector<int> keysOf(Scale scale, Tuning tuning = Tuning::EDO53, int key = 0) {
  Song song(tuning, key);
  song.setScale(scale);
  auto window = song.getScaleDegreesWindow(0, 100);
  std::vector<int> out;
  for (auto value : window) {
    if (value >= key + edoStepsFor(tuning)) break;
    out.push_back(value - key);
  }
  return out;
}

// A scale of the whole gamut, which no scale of the application is: a key of it
// can carry two ratios, one of each side.
JustScale wholeGamut() { return JustScale{gamut::kExtendedDiamond, std::nullopt}; }

// The seventh chords a scale offers, each once, as sets of gamut notes with their roots.
struct Seventh {
  std::set<std::size_t> notes;
  std::size_t root;
  Kind kind;
};
std::vector<Seventh> sevenths(const ScaleChords & chords) {
  std::vector<Seventh> out;
  for (std::size_t d = 0; d < gamut::kExtendedDiamond.size(); d++) {
    for (auto entry : chords.chordsOf(d)) {
      if (entry->kind != Kind::HARMONIC_SEVENTH && entry->kind != Kind::MIRROR_SEVENTH) continue;
      std::set<std::size_t> notes(entry->notes.begin(), entry->notes.end());
      bool seen = std::any_of(out.begin(), out.end(), [&](const Seventh & s) { return s.notes == notes; });
      if (!seen) out.push_back({notes, entry->root, entry->kind});
    }
  }
  return out;
}

} // namespace

TEST(the_diamond_scale_is_named_and_has_its_ratios) {
  CHECK(scaleFromString(to_string(Scale::DIAMOND)) == Scale::DIAMOND);
  CHECK(scaleLabel(Scale::DIAMOND) == "Diamond");
  // There is no extended diamond scale.
  CHECK(scaleFromString("extended-diamond") == Scale::NONE);
  std::vector<Ratio> diamond;
  for (auto i : scaleGamutIndices(Scale::DIAMOND)) diamond.push_back(gamut::kExtendedDiamond[i].ratio);
  CHECK((diamond == std::vector<Ratio>{1 / 1_r, 8 / 7_r, 7 / 6_r, 6 / 5_r, 5 / 4_r, 4 / 3_r, 7 / 5_r, 10 / 7_r, 3 / 2_r, 8 / 5_r, 5 / 3_r, 12 / 7_r, 7 / 4_r}));
  auto names = scaleDegreeNames(Scale::DIAMOND);
  CHECK((names == std::vector<std::string_view>{"C", "E𝄫", "D♯", "E♭", "E", "F", "F♯↑", "G♭↓", "G", "A♭", "A", "B𝄫", "A♯↑"}));
}

TEST(the_diamond_step_view_steps_through_13_keys) {
  CHECK(keysOf(Scale::DIAMOND).size() == 13);
  CHECK(keysOf(Scale::DIAMOND, Tuning::EDO31).size() == 13);
  Song song(Tuning::EDO53, -1);
  song.setScale(Scale::DIAMOND);
  auto window = song.getScaleDegreesWindow(0, 14);
  CHECK(window[13] == 53);
  CHECK(std::is_sorted(window.begin(), window.end()));
}

TEST(the_diamond_layout_gives_the_thirteen_notes_on_thirteen_keys_and_no_other) {
  const std::vector<Ratio> ratios = {1 / 1_r, 8 / 7_r, 7 / 6_r, 6 / 5_r, 5 / 4_r, 4 / 3_r, 7 / 5_r, 10 / 7_r, 3 / 2_r, 8 / 5_r, 5 / 3_r, 12 / 7_r, 7 / 4_r};
  const std::string order = "q2w3ert6y7u8i";
  CHECK(order.size() == ratios.size());
  int previous = -1;
  for (std::size_t i = 0; i < order.size(); i++) {
    auto note = noteOf(order[i], Scale::DIAMOND);
    CHECK(gamut::kExtendedDiamond[note.gamut_index].ratio == ratios[i]);
    CHECK(note.value > previous); // rising
    previous = note.value;
  }
  std::set<char> used(order.begin(), order.end());
  used.insert('o');
  for (int id = 32; id < 127; id++) {
    bool gives = keyToNote(id, Tuning::EDO53, Scale::DIAMOND, 0, 4).has_value();
    CHECK(gives == (used.count(static_cast<char>(id)) > 0));
  }
  CHECK(noteOf('o', Scale::DIAMOND).value == kC + 53);
  std::set<int> values;
  for (auto id : order) values.insert(noteOf(id, Scale::DIAMOND, 0, Tuning::EDO31).value);
  CHECK(values.size() == 13);
  for (auto id : order) {
    auto in_c = noteOf(id, Scale::DIAMOND);
    auto in_a = noteOf(id, Scale::DIAMOND, kA);
    CHECK(in_a.value - in_c.value == kA);
    CHECK_NEAR(in_a.correction_cents, in_c.correction_cents, 1e-9);
  }
}

// ---- A key carrying two ratios, in a scale built for the test ----

TEST(a_key_with_two_ratios_stands_for_the_one_its_correction_is_nearer) {
  auto scale = just_intonation::scaleCorrectionsFor(Tuning::EDO53, wholeGamut(), 0);
  struct Shared {
    int key;
    Ratio nearer_key, other;
  };
  // The ratio nearer the key's own pitch is the one with no correction.
  for (auto shared : {Shared{2, 49 / 48_r, 50 / 49_r}, Shared{5, 16 / 15_r, 15 / 14_r}, Shared{48, 15 / 8_r, 28 / 15_r}, Shared{51, 96 / 49_r, 49 / 25_r}}) {
    CHECK_NEAR(scale.forNote(kC + shared.key).value_or(1e9), correctionOf(shared.nearer_key), 1e-9);
    CHECK_NEAR(scale.forNote(kC + shared.key, 0.0).value_or(1e9), correctionOf(shared.nearer_key), 1e-9);
    CHECK_NEAR(scale.forNote(kC + shared.key, correctionOf(shared.other)).value_or(1e9), correctionOf(shared.other), 1e-9);
    CHECK_NEAR(scale.forNote(kC + shared.key, correctionOf(shared.nearer_key)).value_or(1e9), correctionOf(shared.nearer_key), 1e-9);
    CHECK(scale.ratioForNote(kC + shared.key).value_or(NoteRatio{}).ratio == shared.nearer_key);
    CHECK(scale.ratioForNote(kC + shared.key, correctionOf(shared.other)).value_or(NoteRatio{}).ratio == shared.other);
    // The same in another song key.
    auto in_a = just_intonation::scaleCorrectionsFor(Tuning::EDO53, wholeGamut(), kA);
    CHECK(in_a.ratioForNote(kC + kA + shared.key, correctionOf(shared.other)).value_or(NoteRatio{}).ratio == shared.other);
  }
  // Exactly these four keys carry two ratios.
  int shared_keys = 0;
  for (int step = 0; step < 53; step++) shared_keys += scale.second_present[static_cast<size_t>(step)] ? 1 : 0;
  CHECK(shared_keys == 4);
  // In otonal and utonal songs the scale decides, whatever a note carries.
  auto otonal = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::OTONAL, 0);
  CHECK(otonal.ratioForNote(kC + 48, correctionOf(28 / 15_r)).value_or(NoteRatio{}).ratio == 15 / 8_r);
  auto utonal = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::UTONAL, 0);
  CHECK(utonal.ratioForNote(kC + 48).value_or(NoteRatio{}).ratio == 28 / 15_r);
}

TEST(apply_just_intonation_gives_key_48_the_ratio_its_correction_says) {
  Song song(Tuning::EDO53, kC);
  auto arrangement = song.getArrangement();
  ArrangementBackgroundGrid grid(arrangement);
  std::vector<int> track_ids = {10};
  Note plain(kC + 48, 100);
  Note utonal(kC + 48, 100);
  utonal.setTuningCorrection(correctionOf(28 / 15_r) + 0.4); // roughly 28/15
  Note otonal(kC + 48, 100);
  otonal.setTuningCorrection(correctionOf(15 / 8_r) - 0.3);
  arrangement.setNote(0, 10, 0, plain);
  arrangement.setNote(1, 10, 0, utonal);
  arrangement.setNote(2, 10, 0, otonal);
  IntonationContext context{Tuning::EDO53, kC, just_intonation::scaleCorrectionsFor(Tuning::EDO53, wholeGamut(), kC)};
  applyJustIntonationBlock(grid, 0, 2, track_ids, 0, 0, context, [](int) { return false; });
  auto quantized = [](double cents) { return Note::quantizeTuningCorrection(cents).value_or(1e9); };
  CHECK_NEAR(arrangement.getNotes(0, 10)[0].getTuningCorrectionCents(), quantized(correctionOf(15 / 8_r)), 1e-9);
  CHECK_NEAR(arrangement.getNotes(1, 10)[0].getTuningCorrectionCents(), quantized(correctionOf(28 / 15_r)), 1e-9);
  CHECK_NEAR(arrangement.getNotes(2, 10)[0].getTuningCorrectionCents(), quantized(correctionOf(15 / 8_r)), 1e-9);
  // In a utonal song every note on the key is 28/15.
  context.scale = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::UTONAL, kC);
  applyJustIntonationBlock(grid, 0, 2, track_ids, 0, 0, context, [](int) { return false; });
  for (int row = 0; row < 3; row++) CHECK_NEAR(arrangement.getNotes(row, 10)[0].getTuningCorrectionCents(), quantized(correctionOf(28 / 15_r)), 1e-9);
}

namespace {
// Completes the given notes (note values and ratios) to the first chord of a size.
Completion first(const ScaleChords & scale, std::vector<std::pair<int, Ratio>> given, int size = 4) {
  std::vector<int> values;
  std::map<int, std::size_t> known;
  for (auto & [value, ratio] : given) {
    values.push_back(value);
    known[value] = index(ratio);
  }
  auto result = complete(scale, values, size, known);
  CHECK(result.status == Status::OK);
  return result.chords.empty() ? Completion() : result.chords[0];
}
} // namespace

// The seven-chords that take notes from both scales, in a scale of the whole gamut: the chord
// needs the ratio the note has, B as 28/15 or as 15/8.
TEST(the_chords_of_both_scales_are_completed_in_a_scale_of_the_whole_gamut) {
  ScaleChords scale(Scale::DIAMOND, wholeGamut(), Tuning::EDO53, 0);
  auto step = [](Ratio r) { return kC + r.steps(53); };
  struct Case {
    std::vector<Ratio> given;
    std::vector<Ratio> chord;
  };
  const Case cases[] = {
      {{8 / 5_r, 1 / 1_r, 7 / 5_r}, {8 / 5_r, 1 / 1_r, 6 / 5_r, 7 / 5_r}},
      {{10 / 7_r, 5 / 3_r, 5 / 4_r}, {10 / 7_r, 5 / 3_r, 1 / 1_r, 5 / 4_r}},
      {{12 / 7_r, 15 / 14_r, 9 / 7_r}, {12 / 7_r, 15 / 14_r, 9 / 7_r, 3 / 2_r}},
      {{4 / 3_r, 14 / 9_r, 28 / 15_r}, {4 / 3_r, 14 / 9_r, 28 / 15_r, 7 / 6_r}},
      {{28 / 15_r, 7 / 6_r, 49 / 30_r}, {28 / 15_r, 7 / 6_r, 7 / 5_r, 49 / 30_r}},
      {{60 / 49_r, 10 / 7_r, 15 / 14_r}, {60 / 49_r, 10 / 7_r, 12 / 7_r, 15 / 14_r}},
  };
  for (auto & c : cases) {
    std::vector<std::pair<int, Ratio>> given;
    for (auto ratio : c.given) given.emplace_back(step(ratio) + (ratio < c.given[0] ? 53 : 0), ratio);
    auto chord = first(scale, given);
    std::set<std::size_t> want;
    for (auto ratio : c.chord) want.insert(index(ratio));
    CHECK(std::set<std::size_t>(chord.ratios.begin(), chord.ratios.end()) == want);
    CHECK(chord.notes.size() == 4);
  }
  auto b = kC + 48;
  auto mirror = complete(scale, {kC + 22, kC + 34, b}, 4, {{kC + 22, index(4 / 3_r)}, {kC + 34, index(14 / 9_r)}, {b, index(28 / 15_r)}});
  CHECK(mirror.status == Status::OK);
  auto otonal_b = complete(scale, {kC + 22, kC + 34, b}, 4, {{kC + 22, index(4 / 3_r)}, {kC + 34, index(14 / 9_r)}, {b, index(15 / 8_r)}});
  CHECK(otonal_b.status == Status::NO_CHORD);
  // Without a known ratio, the note's correction decides, and with none the nearer ratio.
  CHECK(scale.degreeOf(b, correctionOf(28 / 15_r)) == index(28 / 15_r));
  CHECK(scale.degreeOf(b) == index(15 / 8_r));
  CHECK(scale.degreeOf(kC + 2, correctionOf(50 / 49_r)) == index(50 / 49_r));
  CHECK(scale.degreeOf(kC + 2) == index(49 / 48_r));
}

TEST(the_diamond_has_exactly_eight_seventh_chords_and_no_chord_outside_its_13_notes) {
  auto found = sevenths(ScaleChords(Scale::DIAMOND, Tuning::EDO53, 0));
  std::vector<std::set<std::size_t>> expected = {
      setOf({1 / 1_r, 5 / 4_r, 3 / 2_r, 7 / 4_r}),
      setOf({4 / 3_r, 5 / 3_r, 1 / 1_r, 7 / 6_r}),
      setOf({1 / 1_r, 7 / 6_r, 7 / 5_r, 7 / 4_r}),
      setOf({8 / 7_r, 4 / 3_r, 8 / 5_r, 1 / 1_r}),
      setOf({12 / 7_r, 1 / 1_r, 6 / 5_r, 3 / 2_r}),
      setOf({8 / 7_r, 10 / 7_r, 12 / 7_r, 1 / 1_r}),
      setOf({8 / 5_r, 1 / 1_r, 6 / 5_r, 7 / 5_r}),
      setOf({10 / 7_r, 5 / 3_r, 1 / 1_r, 5 / 4_r}),
  };
  CHECK(found.size() == 8);
  for (auto & chord : expected) {
    CHECK(std::any_of(found.begin(), found.end(), [&](const Seventh & s) { return s.notes == chord; }));
  }
  std::set<std::size_t> notes13;
  for (auto i : scaleGamutIndices(Scale::DIAMOND)) notes13.insert(i);
  ScaleChords chords(Scale::DIAMOND, Tuning::EDO53, 0);
  for (std::size_t d = 0; d < gamut::kExtendedDiamond.size(); d++) {
    for (int size : {4, 5}) {
      for (auto entry : chords.chordsOf(d, size)) {
        CHECK(notes13.count(d) > 0);
        for (auto note : entry->notes) CHECK(notes13.count(note) > 0);
      }
    }
  }
  CHECK(sevenths(ScaleChords(Scale::DIAMOND, Tuning::EDO31, 0)).size() == 8);
}

// A harmonic seventh is named by its lowest note, a mirror seventh by its top note.
TEST(seventh_chords_are_named_by_their_root) {
  ScaleChords chords(Scale::DIAMOND, Tuning::EDO53, 0);
  auto name_of = [&](std::set<std::size_t> notes) {
    for (std::size_t d = 0; d < gamut::kExtendedDiamond.size(); d++) {
      for (auto entry : chords.chordsOf(d)) {
        if (std::set<std::size_t>(entry->notes.begin(), entry->notes.end()) == notes && entry->notes[0] == entry->root) return chords.chordName(*entry);
      }
    }
    return std::string("none");
  };
  CHECK(name_of(setOf({10 / 7_r, 5 / 3_r, 1 / 1_r, 5 / 4_r})) == "E mirror seventh");
  CHECK(name_of(setOf({1 / 1_r, 7 / 6_r, 7 / 5_r, 7 / 4_r})) == "A♯↑ mirror seventh");
  CHECK(name_of(setOf({8 / 7_r, 4 / 3_r, 8 / 5_r, 1 / 1_r})) == "C mirror seventh");
  CHECK(name_of(setOf({8 / 5_r, 1 / 1_r, 6 / 5_r, 7 / 5_r})) == "A♭ harmonic seventh");
  CHECK(name_of(setOf({1 / 1_r, 5 / 4_r, 3 / 2_r, 7 / 4_r})) == "C harmonic seventh");
}

TEST(a_notes_chords_are_those_of_its_scale_then_the_chords_of_both) {
  // In a scale of the whole gamut, to see the lists for notes outside the diamond's 13.
  ScaleChords chords(Scale::DIAMOND, wholeGamut(), Tuning::EDO53, 0);
  auto of_c = chords.chordsOf(index(1 / 1_r));
  std::vector<Entry> otonal_c = degreeChords(Scale::OTONAL, index(1 / 1_r));
  std::vector<Entry> utonal_c = degreeChords(Scale::UTONAL, index(1 / 1_r));
  CHECK(of_c.size() > otonal_c.size() + utonal_c.size());
  for (std::size_t i = 0; i < otonal_c.size(); i++) CHECK(of_c[i]->notes == otonal_c[i].notes);
  for (std::size_t i = 0; i < utonal_c.size(); i++) CHECK(of_c[otonal_c.size() + i]->notes == utonal_c[i].notes);
  auto a_flat = index(8 / 5_r);
  auto of_a_flat = chords.chordsOf(a_flat);
  auto utonal = degreeChords(Scale::UTONAL, a_flat);
  for (std::size_t i = 0; i < utonal.size(); i++) CHECK(of_a_flat[i]->notes == utonal[i].notes);
  std::vector<std::size_t> wanted = {a_flat, index(1 / 1_r), index(6 / 5_r), index(7 / 5_r)};
  auto at = std::find_if(of_a_flat.begin(), of_a_flat.end(), [&](const Entry * e) { return e->notes == wanted; });
  CHECK(at != of_a_flat.end());
  CHECK(static_cast<std::size_t>(at - of_a_flat.begin()) >= utonal.size());
}
