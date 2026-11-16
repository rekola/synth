#include "TestFramework.h"

#include "../src/model/Chords.h"
#include "../src/model/Division.h"
#include "../src/model/JustIntonation.h"
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

double correctionOf(Ratio ratio, int edo = 53) {
  return ratio.cents() - ratio.steps(edo) * 1200.0 / edo;
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

// Each scale borrows three notes, so it has 21 and the step view steps through 21 keys.
TEST(otonal_and_utonal_have_21_notes_with_three_borrowed) {
  auto ratios = [](Scale scale) {
    std::set<Ratio> out;
    for (auto i : scaleGamutIndices(scale)) out.insert(gamut::kExtendedDiamond[i].ratio);
    return out;
  };
  auto otonal = ratios(Scale::OTONAL);
  auto utonal = ratios(Scale::UTONAL);
  CHECK(otonal.size() == 21);
  CHECK(utonal.size() == 21);
  int otonal_own = 0, utonal_own = 0;
  for (auto & note : gamut::kExtendedDiamond) {
    if (note.side != GamutSide::UTONAL) otonal_own += otonal.count(note.ratio) ? 1 : 0;
    if (note.side != GamutSide::OTONAL) utonal_own += utonal.count(note.ratio) ? 1 : 0;
  }
  CHECK(otonal_own == 18);
  CHECK(utonal_own == 18);
  for (auto ratio : {60 / 49_r, 10 / 7_r, 12 / 7_r}) CHECK(otonal.count(ratio) == 1);
  for (auto ratio : {7 / 6_r, 7 / 5_r, 49 / 30_r}) CHECK(utonal.count(ratio) == 1);
  CHECK(otonal.count(7 / 6_r) == 1); // its own
  CHECK(otonal.count(8 / 7_r) == 0);
  CHECK(utonal.count(5 / 4_r) == 0);
  // The step view: 21 keys, and the window runs on into the next octave.
  CHECK(keysOf(Scale::OTONAL).size() == 21);
  CHECK(keysOf(Scale::UTONAL).size() == 21);
  Song song(Tuning::EDO53, -1);
  song.setScale(Scale::OTONAL);
  CHECK(song.getScaleDegreesWindow(0, 22)[21] == 53);
}

// No key carries two ratios: the tuner holds one ratio on each key, and so do the chords.
TEST(no_key_of_otonal_or_utonal_carries_two_ratios_in_53_edo) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    auto corrections = just_intonation::scaleCorrectionsFor(Tuning::EDO53, scale, 0);
    ScaleChords chords(scale, Tuning::EDO53, 0);
    std::set<int> keys;
    for (auto i : scaleGamutIndices(scale)) {
      int key = gamut::kExtendedDiamond[i].ratio.steps(53) % 53;
      CHECK(keys.insert(key).second); // every one of the 21 notes has a key of its own
      CHECK(corrections.present[static_cast<size_t>(key)]);
      CHECK(corrections.ratios[static_cast<size_t>(key)] == gamut::kExtendedDiamond[i].ratio);
      CHECK(chords.degreeOf(kC + key) == i);
    }
    CHECK(keys.size() == 21);
  }
}

// A borrowed note is a note of the scale in every respect: the tuner gives it its ratio.
TEST(a_borrowed_note_is_tuned_to_its_ratio) {
  auto otonal = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::OTONAL, 0);
  for (auto ratio : {60 / 49_r, 10 / 7_r, 12 / 7_r}) CHECK_NEAR(otonal.forNote(kC + ratio.steps(53)).value_or(1e9), correctionOf(ratio), 1e-9);
  auto utonal = just_intonation::scaleCorrectionsFor(Tuning::EDO53, Scale::UTONAL, kA);
  for (auto ratio : {7 / 6_r, 7 / 5_r, 49 / 30_r}) CHECK_NEAR(utonal.forNote(kC + kA + ratio.steps(53)).value_or(1e9), correctionOf(ratio), 1e-9);
}

// The division is one table; a scale's borrowed notes follow from it.
TEST(the_division_table_gives_each_scale_three_chords_and_the_borrowed_notes_follow) {
  int otonal = 0, utonal = 0;
  for (auto & chord : division::kChords) (chord.given == GamutSide::OTONAL ? otonal : utonal)++;
  CHECK(otonal == 3);
  CHECK(utonal == 3);
  // Every note a chord brings is a note of the other side or of both.
  std::set<Ratio> borrowed_otonal, borrowed_utonal;
  for (auto & note : gamut::kExtendedDiamond) {
    if (division::borrows(GamutSide::OTONAL, note.ratio) && note.side == GamutSide::UTONAL) borrowed_otonal.insert(note.ratio);
    if (division::borrows(GamutSide::UTONAL, note.ratio) && note.side == GamutSide::OTONAL) borrowed_utonal.insert(note.ratio);
  }
  CHECK((borrowed_otonal == std::set<Ratio>{60 / 49_r, 10 / 7_r, 12 / 7_r}));
  CHECK((borrowed_utonal == std::set<Ratio>{7 / 6_r, 7 / 5_r, 49 / 30_r}));
  // Moving the last row to the other scale is a change of the table alone: the chords
  // are then the other scale's, and so are the notes they need.
  auto swapped = division::kChords;
  swapped[2].given = GamutSide::UTONAL;
  swapped[5].given = GamutSide::OTONAL;
  CHECK(division::borrows(swapped, GamutSide::UTONAL, 15 / 14_r)); // B𝄫+ : B𝄫 D♭↓ F♭ G
  CHECK(division::borrows(swapped, GamutSide::OTONAL, 14 / 9_r));  // D♯− : F G♯ B D♯
  CHECK(!division::borrows(division::kChords, GamutSide::OTONAL, 14 / 9_r));
}

// The ten seventh chords of each scale, and each is made of its 21 notes.
TEST(otonal_and_utonal_have_ten_seventh_chords_each) {
  std::vector<std::set<std::size_t>> otonal = {
      setOf({1 / 1_r, 5 / 4_r, 3 / 2_r, 7 / 4_r}),
      setOf({4 / 3_r, 5 / 3_r, 1 / 1_r, 7 / 6_r}),
      setOf({7 / 6_r, 35 / 24_r, 7 / 4_r, 49 / 48_r}),
      setOf({1 / 1_r, 7 / 6_r, 7 / 5_r, 7 / 4_r}),
      setOf({15 / 14_r, 5 / 4_r, 3 / 2_r, 15 / 8_r}),
      setOf({9 / 7_r, 3 / 2_r, 9 / 5_r, 9 / 8_r}),
      setOf({7 / 5_r, 49 / 30_r, 49 / 25_r, 49 / 40_r}),
      setOf({10 / 7_r, 5 / 3_r, 1 / 1_r, 5 / 4_r}),
      setOf({60 / 49_r, 10 / 7_r, 12 / 7_r, 15 / 14_r}),
      setOf({12 / 7_r, 15 / 14_r, 9 / 7_r, 3 / 2_r}),
  };
  std::vector<std::set<std::size_t>> utonal = {
      setOf({8 / 7_r, 4 / 3_r, 8 / 5_r, 1 / 1_r}),
      setOf({12 / 7_r, 1 / 1_r, 6 / 5_r, 3 / 2_r}),
      setOf({96 / 49_r, 8 / 7_r, 48 / 35_r, 12 / 7_r}),
      setOf({8 / 7_r, 10 / 7_r, 12 / 7_r, 1 / 1_r}),
      setOf({16 / 15_r, 4 / 3_r, 8 / 5_r, 28 / 15_r}),
      setOf({16 / 9_r, 10 / 9_r, 4 / 3_r, 14 / 9_r}),
      setOf({80 / 49_r, 50 / 49_r, 60 / 49_r, 10 / 7_r}),
      setOf({8 / 5_r, 1 / 1_r, 6 / 5_r, 7 / 5_r}),
      setOf({28 / 15_r, 7 / 6_r, 7 / 5_r, 49 / 30_r}),
      setOf({4 / 3_r, 14 / 9_r, 28 / 15_r, 7 / 6_r}),
  };
  for (auto [scale, expected] : {std::pair{Scale::OTONAL, otonal}, std::pair{Scale::UTONAL, utonal}}) {
    ScaleChords chords(scale, Tuning::EDO53, 0);
    auto found = sevenths(chords);
    CHECK(found.size() == 10);
    for (auto & chord : expected) {
      CHECK(std::any_of(found.begin(), found.end(), [&](const Seventh & s) { return s.notes == chord; }));
    }
    // A song in the scale offers no chord with a note outside its 21 notes.
    std::set<std::size_t> own;
    for (auto i : scaleGamutIndices(scale)) own.insert(i);
    for (std::size_t d = 0; d < gamut::kExtendedDiamond.size(); d++) {
      for (int size : {4, 5}) {
        auto list = chords.chordsOf(d, size);
        CHECK(list.empty() || own.count(d) > 0);
        for (auto entry : list) {
          for (auto note : entry->notes) CHECK(own.count(note) > 0);
        }
      }
    }
  }
}

// Each seventh chord is named by its root: the lowest note of a harmonic one, the
// top of a mirror one. The ten roots are the keys of the q row, in rising order.
TEST(the_roots_of_the_ten_seventh_chords_are_the_q_row) {
  const std::vector<Ratio> otonal_roots = {1 / 1_r, 15 / 14_r, 9 / 8_r, 7 / 6_r, 49 / 40_r, 5 / 4_r, 4 / 3_r, 12 / 7_r, 7 / 4_r, 15 / 8_r};
  const std::vector<Ratio> utonal_roots = {1 / 1_r, 16 / 15_r, 8 / 7_r, 7 / 6_r, 3 / 2_r, 8 / 5_r, 80 / 49_r, 12 / 7_r, 16 / 9_r, 28 / 15_r};
  const std::string q_row = "qwertyuiop";
  for (auto [scale, roots] : {std::pair{Scale::OTONAL, otonal_roots}, std::pair{Scale::UTONAL, utonal_roots}}) {
    ScaleChords chords(scale, Tuning::EDO53, 0);
    std::set<std::size_t> found;
    for (auto & seventh : sevenths(chords)) found.insert(seventh.root);
    std::set<std::size_t> wanted;
    for (auto ratio : roots) wanted.insert(index(ratio));
    CHECK(found == wanted);
    for (std::size_t i = 0; i < roots.size(); i++) {
      CHECK(gamut::kExtendedDiamond[noteOf(q_row[i], scale).gamut_index].ratio == roots[i]);
      // A root's own chord is the first of its list, read upward from it.
      auto list = chords.chordsOf(index(roots[i]));
      CHECK(!list.empty());
      if (list.empty()) continue;
      CHECK(list[0]->root == index(roots[i]));
      CHECK(list[0]->notes[0] == index(roots[i]));
      CHECK((list[0]->kind == Kind::HARMONIC_SEVENTH || list[0]->kind == Kind::MIRROR_SEVENTH));
    }
  }
  // The three chords a scale gains are named by their roots.
  ScaleChords otonal(Scale::OTONAL, Tuning::EDO53, 0);
  ScaleChords utonal(Scale::UTONAL, Tuning::EDO53, 0);
  auto name_of = [&](const ScaleChords & scale, std::set<std::size_t> notes) {
    for (auto & seventh : sevenths(scale)) {
      if (seventh.notes != notes) continue;
      for (auto entry : scale.chordsOf(seventh.root)) {
        if (std::set<std::size_t>(entry->notes.begin(), entry->notes.end()) == notes && entry->notes[0] == seventh.root) return scale.chordName(*entry);
      }
    }
    return std::string("none");
  };
  CHECK(name_of(otonal, setOf({10 / 7_r, 5 / 3_r, 1 / 1_r, 5 / 4_r})) == "E mirror seventh");
  CHECK(name_of(otonal, setOf({60 / 49_r, 10 / 7_r, 12 / 7_r, 15 / 14_r})) == "D♭↓ mirror seventh");
  CHECK(name_of(otonal, setOf({12 / 7_r, 15 / 14_r, 9 / 7_r, 3 / 2_r})) == "B𝄫 harmonic seventh");
  CHECK(name_of(utonal, setOf({8 / 5_r, 1 / 1_r, 6 / 5_r, 7 / 5_r})) == "A♭ harmonic seventh");
  CHECK(name_of(utonal, setOf({28 / 15_r, 7 / 6_r, 7 / 5_r, 49 / 30_r})) == "B harmonic seventh");
  CHECK(name_of(utonal, setOf({4 / 3_r, 14 / 9_r, 28 / 15_r, 7 / 6_r})) == "D♯ mirror seventh");
}

// What each key of a layout gives, from the tables of the task.
namespace {
struct Place {
  char key;
  Ratio ratio;
  const char * in_c;
};
const std::vector<Place> kOtonalLayout = {
    {'2', 49 / 48_r, "D𝄫"},
    {'7', 7 / 5_r, "F♯↑"},
    {'8', 10 / 7_r, "G♭↓"},
    {'9', 49 / 30_r, "G𝄪"},
    {'0', 9 / 5_r, "B♭"},
    {'q', 1 / 1_r, "C"},
    {'w', 15 / 14_r, "D♭↓"},
    {'e', 9 / 8_r, "D"},
    {'r', 7 / 6_r, "D♯"},
    {'t', 49 / 40_r, "D𝄪"},
    {'y', 5 / 4_r, "E"},
    {'u', 4 / 3_r, "F"},
    {'i', 12 / 7_r, "B𝄫"},
    {'o', 7 / 4_r, "A♯↑"},
    {'p', 15 / 8_r, "B"},
    {'f', 60 / 49_r, "F𝄫"},
    {'g', 9 / 7_r, "F♭"},
    {'h', 35 / 24_r, "F𝄪"},
    {'j', 3 / 2_r, "G"},
    {'k', 5 / 3_r, "A"},
    {'l', 49 / 25_r, "B♯"},
};
const std::vector<Place> kUtonalLayout = {
    {'2', 50 / 49_r, "D𝄫"},
    {'4', 6 / 5_r, "E♭"},
    {'5', 48 / 35_r, "G𝄫"},
    {'6', 7 / 5_r, "F♯↑"},
    {'q', 1 / 1_r, "C"},
    {'w', 16 / 15_r, "D♭↓"},
    {'e', 8 / 7_r, "E𝄫"},
    {'r', 7 / 6_r, "D♯"},
    {'t', 3 / 2_r, "G"},
    {'y', 8 / 5_r, "A♭"},
    {'u', 80 / 49_r, "A♭↑"},
    {'i', 12 / 7_r, "B𝄫"},
    {'o', 16 / 9_r, "B♭↓"},
    {'p', 28 / 15_r, "B"},
    {'s', 10 / 9_r, "D↓"},
    {'d', 60 / 49_r, "F𝄫"},
    {'f', 4 / 3_r, "F"},
    {'g', 10 / 7_r, "G♭↓"},
    {'h', 14 / 9_r, "G♯"},
    {'j', 49 / 30_r, "G𝄪"},
    {'l', 96 / 49_r, "B♯"},
};

// Where a key stands over the q row: the q row is 0..9; a number-row key 2..0 and an
// a-row key s..l (from s) lie between two of its keys, at 0.5, 1.5, ...
double position(char key) {
  const std::string digits = "234567890";
  const std::string a_row = "sdfghjkl";
  if (auto at = digits.find(key); at != std::string::npos) return static_cast<double>(at) + 0.5;
  if (auto at = a_row.find(key); at != std::string::npos) return static_cast<double>(at) + 1.5;
  return static_cast<double>(std::string("qwertyuiop").find(key));
}

void checkLayout(Scale scale, const std::vector<Place> & layout) {
  std::set<char> used;
  const std::string q_row = "qwertyuiop";
  for (auto & place : layout) {
    used.insert(place.key);
    for (int song_key : {0, kA, 9}) {
      auto note = noteOf(place.key, scale, song_key);
      CHECK(gamut::kExtendedDiamond[note.gamut_index].ratio == place.ratio);
      CHECK(note.value == kC + song_key + place.ratio.steps(53));
      CHECK_NEAR(note.correction_cents, correctionOf(place.ratio), 1e-9);
    }
    CHECK(noteOf(place.key, scale).value == Note(place.in_c, Tuning::EDO53, 0x28, 0, 4).getValue());
  }
  CHECK(used.size() == 21);
  // Nothing else gives a note: not the z row, not the other digits, not the note-off key.
  for (int id = 32; id < 127; id++) CHECK(keyToNote(id, Tuning::EDO53, scale, 0, 4).has_value() == (used.count(static_cast<char>(id)) > 0));
  CHECK(!keyToNote('a', Tuning::EDO53, scale, 0, 4));
  for (auto id : std::string("zxcvbnm")) CHECK(!keyToNote(id, Tuning::EDO53, scale, 0, 4));
  // Each row rises from left to right.
  auto rising = [&](std::vector<char> row) {
    for (std::size_t i = 1; i < row.size(); i++) {
      if (!(noteOf(row[i - 1], scale).value < noteOf(row[i], scale).value)) return false;
    }
    return true;
  };
  std::vector<char> numbers, home;
  for (auto & place : layout) {
    if (std::string("234567890").find(place.key) != std::string::npos) numbers.push_back(place.key);
    if (std::string("sdfghjkl").find(place.key) != std::string::npos) home.push_back(place.key);
  }
  std::sort(numbers.begin(), numbers.end(), [](char a, char b) { return position(a) < position(b); });
  std::sort(home.begin(), home.end(), [](char a, char b) { return position(a) < position(b); });
  CHECK(rising(numbers));
  CHECK(rising(home));
  CHECK(rising(std::vector<char>(q_row.begin(), q_row.end())));
  // No note is more than one key from its place between the roots.
  std::vector<Ratio> roots;
  for (auto id : q_row) roots.push_back(gamut::kExtendedDiamond[noteOf(id, scale).gamut_index].ratio);
  for (auto & place : layout) {
    if (q_row.find(place.key) != std::string::npos) continue;
    std::size_t below = 0; // the roots below the note
    for (auto & root : roots) below += (root < place.ratio) ? 1u : 0u;
    double between = static_cast<double>(below) - 0.5; // the place after the last root below it
    CHECK(std::fabs(position(place.key) - between) <= 1.0 + 1e-9);
  }
}
} // namespace

TEST(the_otonal_layout_gives_every_ratio_of_its_table_in_any_key) {
  checkLayout(Scale::OTONAL, kOtonalLayout);
  // G is not a root: it is on j.
  CHECK(gamut::kExtendedDiamond[noteOf('j', Scale::OTONAL).gamut_index].ratio == 3 / 2_r);
}

TEST(the_utonal_layout_gives_every_ratio_of_its_table_in_any_key) {
  checkLayout(Scale::UTONAL, kUtonalLayout);
  // F is not a root: it is on f.
  CHECK(gamut::kExtendedDiamond[noteOf('f', Scale::UTONAL).gamut_index].ratio == 4 / 3_r);
}

TEST(the_layouts_give_the_same_ratios_in_31_edo_where_they_have_a_key) {
  // In 31-EDO a note whose key an own note has is not on its key; the rest are.
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    auto corrections = just_intonation::scaleCorrectionsFor(Tuning::EDO31, scale, 0);
    int with_key = 0;
    for (int id = 32; id < 127; id++) {
      auto note = keyToNote(id, Tuning::EDO31, scale, 0, 4);
      if (!note) continue;
      CHECK(corrections.forNote(note->value).has_value());
      with_key++;
    }
    CHECK(with_key == 21);
  }
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

// A note's chords are those of otonal, then those of utonal, then the chords that take notes from both.
// (The otonal and utonal lists here also hold the chords with a borrowed note; the diamond's do not.)
TEST(a_notes_chords_are_those_of_its_scale_then_the_chords_of_both) {
  auto own = [](const std::vector<Entry> & list, GamutSide side) {
    std::vector<Entry> out;
    for (auto & entry : list) {
      bool borrowed = false;
      for (auto note : entry.notes) {
        auto s = gamut::kExtendedDiamond[note].side;
        borrowed = borrowed || (s != GamutSide::BOTH && s != side);
      }
      if (!borrowed) out.push_back(entry);
    }
    return out;
  };
  auto of_c = degreeChords(Scale::DIAMOND, index(1 / 1_r));
  auto otonal_c = own(degreeChords(Scale::OTONAL, index(1 / 1_r)), GamutSide::OTONAL);
  auto utonal_c = own(degreeChords(Scale::UTONAL, index(1 / 1_r)), GamutSide::UTONAL);
  CHECK(of_c.size() > otonal_c.size() + utonal_c.size());
  for (std::size_t i = 0; i < otonal_c.size(); i++) CHECK(of_c[i].notes == otonal_c[i].notes);
  for (std::size_t i = 0; i < utonal_c.size(); i++) CHECK(of_c[otonal_c.size() + i].notes == utonal_c[i].notes);
  // A♭ is utonal: its utonal chords, then the chords of both, among them A♭ C E♭ F♯↑.
  auto a_flat = index(8 / 5_r);
  auto of_a_flat = degreeChords(Scale::DIAMOND, a_flat);
  auto utonal = own(degreeChords(Scale::UTONAL, a_flat), GamutSide::UTONAL);
  for (std::size_t i = 0; i < utonal.size(); i++) CHECK(of_a_flat[i].notes == utonal[i].notes);
  std::vector<std::size_t> wanted = {a_flat, index(1 / 1_r), index(6 / 5_r), index(7 / 5_r)};
  auto at = std::find_if(of_a_flat.begin(), of_a_flat.end(), [&](const Entry & e) { return e.notes == wanted; });
  CHECK(at != of_a_flat.end());
  CHECK(static_cast<std::size_t>(at - of_a_flat.begin()) >= utonal.size());
}

// What 31-EDO keeps of the borrowed notes and chords is what its keys hold; no own note
// loses its key to a borrowed one.
TEST(in_31_edo_a_borrowed_note_never_takes_the_key_of_an_own_note) {
  for (auto scale : {Scale::OTONAL, Scale::UTONAL}) {
    auto side = scale == Scale::OTONAL ? GamutSide::OTONAL : GamutSide::UTONAL;
    auto corrections = just_intonation::scaleCorrectionsFor(Tuning::EDO31, scale, 0);
    ScaleChords chords(scale, Tuning::EDO31, 0);
    for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
      auto & note = gamut::kExtendedDiamond[i];
      if (note.side != GamutSide::BOTH && note.side != side) continue; // an own note
      int key = note.ratio.steps(31) % 31;
      // Whatever holds an own note's key is an own note, in the tuner and in the chords.
      auto held = gamut::kExtendedDiamond[chords.degreeOf(key).value_or(i)].side;
      CHECK(held == GamutSide::BOTH || held == side);
      Ratio tuned = corrections.ratios[static_cast<size_t>(key)];
      bool own = false;
      for (auto & other : gamut::kExtendedDiamond) own = own || (other.ratio == tuned && (other.side == GamutSide::BOTH || other.side == side));
      CHECK(own);
    }
  }
}
