#include "TestFramework.h"

#include "../src/model/Chords.h"
#include "../src/model/Note.h"
#include "../src/model/Song.h"
#include "../src/playback/KeyboardNotes.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace chords;
using namespace ratio_literals;

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

// A note's list starts with the chords it names, the seventh chords of otonal's direction first
// (upward) in the diamond; the others keep their order: otonal's, utonal's, then those of both.
TEST(a_notes_chords_are_those_it_names_then_those_of_its_scales_then_those_of_both) {
  auto name_of = [](Scale scale, std::size_t note, std::size_t at) {
    ScaleChords chords(scale, Tuning::EDO53, 0);
    auto list = chords.chordsOf(note);
    return at < list.size() ? chords.chordName(*list[at]) : std::string();
  };
  // C names an upward and a downward seventh chord: the upward one comes first in the diamond, the
  // downward one in utonal.
  CHECK(name_of(Scale::DIAMOND, index(1 / 1_r), 0) == "C harmonic seventh");
  CHECK(name_of(Scale::DIAMOND, index(1 / 1_r), 1) == "C mirror seventh");
  CHECK(name_of(Scale::UTONAL, index(1 / 1_r), 0) == "C mirror seventh");
  CHECK(name_of(Scale::OTONAL, index(1 / 1_r), 0) == "C harmonic seventh");
  // A♭ names the chord A♭ C E♭ F♯↑, which takes notes from both scales.
  CHECK(name_of(Scale::DIAMOND, index(8 / 5_r), 0) == "A♭ harmonic seventh");
  // E names the mirror seventh G♭↓ A C E.
  CHECK(name_of(Scale::DIAMOND, index(5 / 4_r), 0) == "E mirror seventh");
  // The chords a note does not name keep their order: otonal's before utonal's.
  auto of_c = degreeChords(Scale::DIAMOND, index(1 / 1_r));
  auto position = [&](std::vector<std::size_t> notes) {
    for (std::size_t i = 0; i < of_c.size(); i++) {
      if (of_c[i].notes == notes) return static_cast<int>(i);
    }
    return -1;
  };
  int otonal = position({index(1 / 1_r), index(7 / 6_r), index(4 / 3_r), index(5 / 3_r)});  // C D♯ F A
  int utonal = position({index(1 / 1_r), index(6 / 5_r), index(3 / 2_r), index(12 / 7_r)}); // C E♭ G B𝄫
  CHECK(otonal >= 0 && utonal >= 0);
  CHECK(otonal < utonal);
}
