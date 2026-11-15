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

cellui::Input key(int id) {
  return cellui::Input(id, 0, 0, false, false, false, false, cellui::Input::Kind::PRESS);
}

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

// The keys of 53-EDO a scale's notes lie on, ascending.
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

} // namespace

TEST(the_diamond_scales_are_named_and_have_their_ratios) {
  CHECK(scaleFromString(to_string(Scale::DIAMOND)) == Scale::DIAMOND);
  CHECK(scaleFromString(to_string(Scale::EXTENDED_DIAMOND)) == Scale::EXTENDED_DIAMOND);
  CHECK(scaleLabel(Scale::DIAMOND) == "Diamond");
  CHECK(scaleLabel(Scale::EXTENDED_DIAMOND) == "Extended diamond");
  std::vector<Ratio> diamond;
  for (auto i : scaleGamutIndices(Scale::DIAMOND)) diamond.push_back(gamut::kExtendedDiamond[i].ratio);
  CHECK((diamond == std::vector<Ratio>{1 / 1_r, 8 / 7_r, 7 / 6_r, 6 / 5_r, 5 / 4_r, 4 / 3_r, 7 / 5_r, 10 / 7_r, 3 / 2_r, 8 / 5_r, 5 / 3_r, 12 / 7_r, 7 / 4_r}));
  CHECK(scaleGamutIndices(Scale::EXTENDED_DIAMOND).size() == 33);
  auto names = scaleDegreeNames(Scale::DIAMOND);
  CHECK((names == std::vector<std::string_view>{"C", "E𝄫", "D♯", "E♭", "E", "F", "F♯↑", "G♭↓", "G", "A♭", "A", "B𝄫", "A♯↑"}));
}

TEST(the_step_view_steps_through_13_and_29_keys) {
  CHECK(keysOf(Scale::DIAMOND).size() == 13);
  CHECK(keysOf(Scale::EXTENDED_DIAMOND).size() == 29);
  // A window runs on into the next octave.
  Song song(Tuning::EDO53, -1);
  song.setScale(Scale::EXTENDED_DIAMOND);
  auto window = song.getScaleDegreesWindow(0, 30);
  CHECK(window[29] == 53);
  CHECK(std::is_sorted(window.begin(), window.end()));
  // In 31-EDO the extended diamond has the keys of the two scales.
  std::set<int> union_keys;
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    Song song31(Tuning::EDO31, -1);
    song31.setScale(scale);
    for (auto value : song31.getScaleDegreesWindow(0, 18)) {
      if (value < 31) union_keys.insert(value);
    }
  }
  auto extended = keysOf(Scale::EXTENDED_DIAMOND, Tuning::EDO31);
  CHECK(std::set<int>(extended.begin(), extended.end()) == union_keys);
  CHECK(extended.size() == union_keys.size());
  // The diamond has a key for each note in 31-EDO too.
  CHECK(keysOf(Scale::DIAMOND, Tuning::EDO31).size() == 13);
}

TEST(a_key_with_two_ratios_stands_for_the_one_its_correction_is_nearer) {
  auto scale = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::EXTENDED_DIAMOND, 0);
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
    auto in_a = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::EXTENDED_DIAMOND, kA);
    CHECK(in_a.ratioForNote(kC + kA + shared.key, correctionOf(shared.other)).value_or(NoteRatio{}).ratio == shared.other);
  }
  // Otonal and utonal songs keep the key's one ratio of the scale, whatever a note carries.
  auto otonal = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::OTONAL, 0);
  CHECK(otonal.ratioForNote(kC + 48, correctionOf(28 / 15_r)).value_or(NoteRatio{}).ratio == 15 / 8_r);
  auto utonal = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::UTONAL, 0);
  CHECK(utonal.ratioForNote(kC + 48).value_or(NoteRatio{}).ratio == 28 / 15_r);
}

TEST(apply_just_intonation_gives_key_48_the_ratio_its_correction_says) {
  Song song(Tuning::EDO53, kC);
  song.setScale(Scale::EXTENDED_DIAMOND);
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
  IntonationContext context{Tuning::EDO53, kC, just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::EXTENDED_DIAMOND, kC)};
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

// The layout: the key for each ratio, in the order of the table of the task.
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
  CHECK(!keyToNote('a', Tuning::EDO53, Scale::DIAMOND, 0, 4));
  // o is the tonic of the next octave.
  CHECK(noteOf('o', Scale::DIAMOND).value == kC + 53);
  // The names in C.
  CHECK(noteOf('t', Scale::DIAMOND).value == Note("F♯↑", Tuning::EDO53, 0x28, 0, 4).getValue());
  // In 31-EDO each of the thirteen notes has a key of its own.
  std::set<int> values;
  for (auto id : order) values.insert(noteOf(id, Scale::DIAMOND, 0, Tuning::EDO31).value);
  CHECK(values.size() == 13);
  // In another key the keys give the same ratios above that tonic.
  for (auto id : order) {
    auto in_c = noteOf(id, Scale::DIAMOND);
    auto in_a = noteOf(id, Scale::DIAMOND, kA);
    CHECK(in_a.value - in_c.value == kA);
    CHECK_NEAR(in_a.correction_cents, in_c.correction_cents, 1e-9);
  }
}

TEST(the_extended_diamond_layout_gives_every_ratio_of_its_table) {
  struct Row {
    char key;
    Ratio ratio;
    const char * in_c;
  };
  const Row table[] = {
      {'q', 1 / 1_r, "C"},
      {'w', 49 / 48_r, "D𝄫"},
      {'2', 50 / 49_r, "D𝄫"},
      {'e', 15 / 14_r, "D♭↓"},
      {'3', 16 / 15_r, "D♭↓"},
      {'r', 9 / 8_r, "D"},
      {'4', 10 / 9_r, "D↓"},
      {'t', 7 / 6_r, "D♯"},
      {'5', 8 / 7_r, "E𝄫"},
      {'y', 49 / 40_r, "D𝄪"},
      {'6', 6 / 5_r, "E♭"},
      {'u', 5 / 4_r, "E"},
      {'7', 60 / 49_r, "F𝄫"},
      {'i', 9 / 7_r, "F♭"},
      {'o', 4 / 3_r, "F"},
      {'p', 7 / 5_r, "F♯↑"},
      {'0', 48 / 35_r, "G𝄫"},
      {'s', 35 / 24_r, "F𝄪"},
      {'z', 10 / 7_r, "G♭↓"},
      {'d', 3 / 2_r, "G"},
      {'x', 14 / 9_r, "G♯"},
      {'f', 49 / 30_r, "G𝄪"},
      {'c', 8 / 5_r, "A♭"},
      {'g', 5 / 3_r, "A"},
      {'v', 80 / 49_r, "A♭↑"},
      {'h', 7 / 4_r, "A♯↑"},
      {'b', 12 / 7_r, "B𝄫"},
      {'j', 9 / 5_r, "B♭"},
      {'n', 16 / 9_r, "B♭↓"},
      {'k', 15 / 8_r, "B"},
      {'m', 28 / 15_r, "B"},
      {'l', 49 / 25_r, "B♯"},
      {',', 96 / 49_r, "B♯"},
  };
  std::set<std::size_t> seen;
  for (auto & row : table) {
    auto note = noteOf(row.key, Scale::EXTENDED_DIAMOND);
    CHECK(gamut::kExtendedDiamond[note.gamut_index].ratio == row.ratio);
    seen.insert(note.gamut_index);
    CHECK(note.value == Note(row.in_c, Tuning::EDO53, 0x28, 0, 4).getValue());
    CHECK(note.value == kC + row.ratio.steps(53));
    CHECK_NEAR(note.correction_cents, correctionOf(row.ratio), 1e-9);
    // The note in another song key has the same ratio and correction.
    auto in_a = noteOf(row.key, Scale::EXTENDED_DIAMOND, kA);
    CHECK(in_a.value - note.value == kA);
    CHECK(in_a.gamut_index == note.gamut_index);
    CHECK_NEAR(in_a.correction_cents, note.correction_cents, 1e-9);
  }
  CHECK(seen.size() == 33);
  // K and M are both a B on key 48, with the correction of 15/8 and of 28/15.
  auto k = noteOf('k', Scale::EXTENDED_DIAMOND);
  auto m = noteOf('m', Scale::EXTENDED_DIAMOND);
  CHECK(k.value == m.value && k.value == kC + 48);
  CHECK_NEAR(k.correction_cents, correctionOf(15 / 8_r), 1e-9);
  CHECK_NEAR(m.correction_cents, correctionOf(28 / 15_r), 1e-9);
  // The digits 1 and 8 and the letter a give nothing; 9 is the tonic of the next octave.
  for (auto id : {'1', '8', 'a'}) CHECK(!keyToNote(id, Tuning::EDO53, Scale::EXTENDED_DIAMOND, 0, 4));
  CHECK(noteOf('9', Scale::EXTENDED_DIAMOND).value == kC + 53);
  // Every key gives a note, 34 in all, and nothing else does.
  int giving = 0;
  for (int id = 32; id < 127; id++) giving += keyToNote(id, Tuning::EDO53, Scale::EXTENDED_DIAMOND, 0, 4).has_value() ? 1 : 0;
  CHECK(giving == 34);
}

namespace {
// The chords of a degree, as sets of ratios, by kind.
std::vector<std::set<std::size_t>> sevenths(Scale scale, Tuning tuning = Tuning::EDO53) {
  ScaleChords chords(scale, tuning, 0);
  std::vector<std::set<std::size_t>> out;
  for (auto d : scaleGamutIndices(scale)) {
    for (auto entry : chords.chordsOf(d)) {
      if (entry->kind != Kind::HARMONIC_SEVENTH && entry->kind != Kind::MIRROR_SEVENTH) continue;
      std::set<std::size_t> notes(entry->notes.begin(), entry->notes.end());
      if (std::find(out.begin(), out.end(), notes) == out.end()) out.push_back(notes);
    }
  }
  return out;
}
} // namespace

TEST(the_diamond_has_exactly_eight_seventh_chords_and_no_chord_outside_its_13_notes) {
  auto found = sevenths(Scale::DIAMOND);
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
  for (auto & chord : expected) CHECK(std::find(found.begin(), found.end(), chord) != found.end());
  // Every chord offered, of any size, is made of the 13 notes.
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
  // The same in 31-EDO, where each note has a key of its own.
  CHECK(sevenths(Scale::DIAMOND, Tuning::EDO31).size() == 8);
}

TEST(a_notes_chords_are_those_of_its_scale_then_the_chords_of_both) {
  ScaleChords chords(Scale::EXTENDED_DIAMOND, Tuning::EDO53, 0);
  // C belongs to both scales: its otonal chords before its utonal ones, then the chords of both.
  auto of_c = chords.chordsOf(index(1 / 1_r));
  std::vector<Entry> otonal_c = degreeChords(Scale::OTONAL, index(1 / 1_r));
  std::vector<Entry> utonal_c = degreeChords(Scale::UTONAL, index(1 / 1_r));
  CHECK(of_c.size() > otonal_c.size() + utonal_c.size());
  for (std::size_t i = 0; i < otonal_c.size(); i++) CHECK(of_c[i]->notes == otonal_c[i].notes);
  for (std::size_t i = 0; i < utonal_c.size(); i++) CHECK(of_c[otonal_c.size() + i]->notes == utonal_c[i].notes);
  // A♭ is utonal: its utonal chords, then the chords of both, among them A♭ C E♭ F♯↑.
  auto a_flat = index(8 / 5_r);
  auto of_a_flat = chords.chordsOf(a_flat);
  auto utonal = degreeChords(Scale::UTONAL, a_flat);
  for (std::size_t i = 0; i < utonal.size(); i++) CHECK(of_a_flat[i]->notes == utonal[i].notes);
  std::vector<std::size_t> wanted = {a_flat, index(1 / 1_r), index(6 / 5_r), index(7 / 5_r)};
  auto at = std::find_if(of_a_flat.begin(), of_a_flat.end(), [&](const Entry * e) { return e->notes == wanted; });
  CHECK(at != of_a_flat.end());
  CHECK(static_cast<std::size_t>(at - of_a_flat.begin()) >= utonal.size());
  CHECK(chords.chordName(**at) == "A♭ harmonic seventh");
  // The otonal and utonal scales have none of the chords of both.
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    ScaleChords one(scale, Tuning::EDO53, 0);
    auto expected = degreeChords(scale, index(1 / 1_r));
    CHECK(one.chordsOf(index(1 / 1_r)).size() == expected.size());
  }
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

std::vector<Ratio> ratiosOf(const Completion & chord) {
  std::vector<Ratio> out;
  for (auto note : chord.ratios) out.push_back(gamut::kExtendedDiamond[note].ratio);
  return out;
}
} // namespace

// The six seventh chords of both scales.
TEST(the_seventh_chords_of_both_scales_are_completed_and_named) {
  ScaleChords scale(Scale::EXTENDED_DIAMOND, Tuning::EDO53, 0);
  auto step = [](Ratio r) { return kC + r.steps(53); };
  struct Case {
    std::vector<Ratio> given;
    std::vector<Ratio> chord;
    const char * name;
  };
  const Case cases[] = {
      {{8 / 5_r, 1 / 1_r, 7 / 5_r}, {8 / 5_r, 1 / 1_r, 6 / 5_r, 7 / 5_r}, "A♭ harmonic seventh"},
      {{10 / 7_r, 5 / 3_r, 5 / 4_r}, {10 / 7_r, 5 / 3_r, 1 / 1_r, 5 / 4_r}, "G♭↓ mirror seventh"},
      {{12 / 7_r, 15 / 14_r, 9 / 7_r}, {12 / 7_r, 15 / 14_r, 9 / 7_r, 3 / 2_r}, "B𝄫 harmonic seventh"},
      {{4 / 3_r, 14 / 9_r, 28 / 15_r}, {4 / 3_r, 14 / 9_r, 28 / 15_r, 7 / 6_r}, "F mirror seventh"},
      {{28 / 15_r, 7 / 6_r, 49 / 30_r}, {28 / 15_r, 7 / 6_r, 7 / 5_r, 49 / 30_r}, "B harmonic seventh"},
      {{60 / 49_r, 10 / 7_r, 15 / 14_r}, {60 / 49_r, 10 / 7_r, 12 / 7_r, 15 / 14_r}, "F𝄫 mirror seventh"},
  };
  for (auto & c : cases) {
    std::vector<std::pair<int, Ratio>> given;
    // Played upward from the chord's lowest note, as the chord is written.
    for (auto ratio : c.given) given.emplace_back(step(ratio) + (ratio < c.given[0] ? 53 : 0), ratio);
    auto chord = first(scale, given);
    // The notes of the chord rise from the lowest given note, and each is its own ratio.
    std::set<std::size_t> want;
    for (auto ratio : c.chord) want.insert(index(ratio));
    CHECK(std::set<std::size_t>(chord.ratios.begin(), chord.ratios.end()) == want);
    CHECK(chord.notes.size() == 4);
    CHECK(chord.name == c.name);
  }
  // B as 28/15 or as 15/8 on the same key: the chord needs the ratio the note has.
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
