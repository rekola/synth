#include "Chords.h"
#include "Note.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <sstream>
#include <string_view>

using namespace std;

namespace chords {
namespace {

// Each chord upward from its degree, the first note. The order within a degree
// is the order they are offered in: seventh chords, a stand-alone triad, then
// the major sevenths and the 7♯9 chords.
constexpr array kOtonalChords = {
    "C E G A♯↑",
    "C D♯ F A",
    "C D♯ F♯↑ A♯↑",
    "C E F A",
    "C E G B",
    "C D♯ E A♯↑",
    "D𝄫 D♯ F𝄪 A♯↑",
    "D♭↓ E G B",
    "D F♭ G B♭",
    "D♯ F A C",
    "D♯ F♯↑ A♯↑ C",
    "D♯ F𝄪 A♯↑ D𝄫",
    "D♯ E A♯↑ C",
    "D𝄪 F♯↑ G𝄪 B♯",
    "D𝄪 F♯↑ G𝄪 A♯↑",
    "E G A♯↑ C",
    "E G B D♭↓",
    "E F A C",
    "E G B C",
    "E A♯↑ C D♯",
    "F♭ G B♭ D",
    "F A C D♯",
    "F A C E",
    "F♯↑ A♯↑ C D♯",
    "F♯↑ G𝄪 B♯ D𝄪",
    "F♯↑ G𝄪 A♯↑ D𝄪",
    "F𝄪 A♯↑ D𝄫 D♯",
    "G A♯↑ C E",
    "G B D♭↓ E",
    "G B♭ D F♭",
    "G B C E",
    "G𝄪 B♯ D𝄪 F♯↑",
    "G𝄪 A♯↑ D𝄪 F♯↑",
    "A C D♯ F",
    "A C E",
    "A C E F",
    "A♯↑ C E G",
    "A♯↑ C D♯ F♯↑",
    "A♯↑ D𝄫 D♯ F𝄪",
    "A♯↑ C D♯ E",
    "A♯↑ D𝄪 F♯↑ G𝄪",
    "B♭ D F♭ G",
    "B D♭↓ E G",
    "B C E G",
    "B♯ D𝄪 F♯↑ G𝄪",
};

constexpr array kUtonalChords = {
    "C E𝄫 F A♭",
    "C E♭ G B𝄫",
    "C E𝄫 G♭↓ B𝄫",
    "C E♭ G A♭",
    "C D♭↓ F A♭",
    "C E𝄫 F G♭↓",
    "D𝄫 F𝄫 G♭↓ A♭↑",
    "D♭↓ F A♭ B",
    "D♭↓ F A♭ C",
    "D↓ F G♯ B♭↓",
    "E𝄫 F A♭ C",
    "E𝄫 G♭↓ B𝄫 C",
    "E𝄫 G𝄫 B𝄫 B♯",
    "E𝄫 F G♭↓ C",
    "E𝄫 F𝄫 B𝄫 B♯",
    "E♭ G B𝄫 C",
    "E♭ G A♭ C",
    "E♭ G𝄫 A♭ B𝄫",
    "F𝄫 G♭↓ A♭↑ D𝄫",
    "F𝄫 B𝄫 B♯ E𝄫",
    "F A♭ C E𝄫",
    "F A♭ B D♭↓",
    "F G♯ B♭↓ D↓",
    "F A♭ C D♭↓",
    "F G♭↓ C E𝄫",
    "G𝄫 B𝄫 B♯ E𝄫",
    "G𝄫 A♭ B𝄫 E♭",
    "G♭↓ B𝄫 C E𝄫",
    "G♭↓ A♭↑ D𝄫 F𝄫",
    "G♭↓ C E𝄫 F",
    "G B𝄫 C E♭",
    "G A♭ C E♭",
    "G♯ B♭↓ D↓ F",
    "A♭ C E𝄫 F",
    "A♭ B D♭↓ F",
    "A♭ C E♭",
    "A♭ C E♭ G",
    "A♭ C D♭↓ F",
    "A♭ B𝄫 E♭ G𝄫",
    "A♭↑ D𝄫 F𝄫 G♭↓",
    "B𝄫 C E♭ G",
    "B𝄫 C E𝄫 G♭↓",
    "B𝄫 B♯ E𝄫 G𝄫",
    "B𝄫 E♭ G𝄫 A♭",
    "B𝄫 B♯ E𝄫 F𝄫",
    "B♭↓ D↓ F G♯",
    "B D♭↓ F A♭",
    "B♯ E𝄫 G𝄫 B𝄫",
    "B♯ E𝄫 F𝄫 B𝄫",
};

// A chord's form: its notes above the one it is named after.
struct Form {
  Kind kind;
  vector<Ratio> above;
};

const vector<Form> & forms() {
  static const vector<Form> table = {
      {Kind::HARMONIC_SEVENTH, {Ratio{5, 4}, Ratio{3, 2}, Ratio{7, 4}}}, // 4:5:6:7
      {Kind::MIRROR_SEVENTH, {Ratio{7, 6}, Ratio{7, 5}, Ratio{7, 4}}},   // 1/7:1/6:1/5:1/4
      {Kind::MAJOR_SEVENTH, {Ratio{5, 4}, Ratio{3, 2}, Ratio{15, 8}}},   // 8:10:12:15
      {Kind::SEVEN_SHARP_NINE, {Ratio{7, 6}, Ratio{5, 4}, Ratio{7, 4}}}, // 12:14:15:21
      {Kind::MAJOR, {Ratio{5, 4}, Ratio{3, 2}}},
      {Kind::MINOR, {Ratio{6, 5}, Ratio{3, 2}}},
  };
  return table;
}

const char * kindName(Kind kind) {
  switch (kind) {
    case Kind::HARMONIC_SEVENTH:
      return "harmonic seventh";
    case Kind::MIRROR_SEVENTH:
      return "mirror seventh";
    case Kind::MAJOR_SEVENTH:
      return "major seventh";
    case Kind::SEVEN_SHARP_NINE:
      return "7♯9";
    case Kind::MAJOR:
      return "major";
    case Kind::MINOR:
      return "minor";
  }
  return "";
}

Ratio ratioOf(size_t index) { return gamut::kExtendedDiamond[index].ratio; }

// The notes' ratios above `root`, within the octave and ascending, without the root.
vector<Ratio> aboveRoot(const vector<size_t> & notes, size_t root) {
  vector<Ratio> above;
  for (auto note : notes) {
    if (note != root) above.push_back((ratioOf(note) / ratioOf(root)).reduced());
  }
  sort(above.begin(), above.end());
  return above;
}

optional<size_t> gamutIndex(string_view spelling, GamutSide side) {
  for (size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
    auto & note = gamut::kExtendedDiamond[i];
    if (note.spelling == spelling && (note.side == GamutSide::BOTH || note.side == side)) return i;
  }
  return nullopt;
}

using ChordTable = map<size_t, vector<Entry>>;

template <size_t N>
ChordTable buildTable(const array<const char *, N> & chords, GamutSide side) {
  ChordTable table;
  for (auto text : chords) {
    Entry entry;
    istringstream words(text);
    for (string word; words >> word;) {
      // A spelling the gamut lacks leaves the chord out.
      if (auto index = gamutIndex(word, side))
        entry.notes.push_back(*index);
      else
        entry.notes.clear(), words.setstate(ios::failbit);
    }
    bool named = false;
    for (auto root : entry.notes) {
      auto above = aboveRoot(entry.notes, root);
      for (auto & form : forms()) {
        if (form.above != above) continue;
        entry.kind = form.kind;
        entry.root = root;
        named = true;
      }
    }
    if (named) table[entry.notes[0]].push_back(std::move(entry));
  }
  return table;
}

const ChordTable & tableFor(Scale scale) {
  static const ChordTable otonal = buildTable(kOtonalChords, GamutSide::OTONAL);
  static const ChordTable utonal = buildTable(kUtonalChords, GamutSide::UTONAL);
  static const ChordTable none;
  return scale == Scale::OTONAL ? otonal : scale == Scale::UTONAL ? utonal
                                                                  : none;
}

} // namespace

const vector<Entry> & degreeChords(Scale scale, size_t gamut_index) {
  static const vector<Entry> none;
  auto & table = tableFor(scale);
  auto it = table.find(gamut_index);
  return it == table.end() ? none : it->second;
}

optional<string> triadName(Ratio second, Ratio third) {
  struct Triad {
    Ratio second, third;
    const char * name;
  };
  static const array<Triad, 6> triads = {{
      {Ratio{5, 4}, Ratio{3, 2}, "major"},
      {Ratio{6, 5}, Ratio{3, 2}, "minor"},
      {Ratio{7, 6}, Ratio{4, 3}, "narrow major"},
      {Ratio{8, 7}, Ratio{4, 3}, "narrow minor"},
      {Ratio{6, 5}, Ratio{7, 5}, "septimal diminished"},
      {Ratio{7, 6}, Ratio{7, 5}, "mirror diminished"},
  }};
  for (auto & triad : triads) {
    if (triad.second == second && triad.third == third) return string(triad.name);
  }
  return nullopt;
}

ScaleChords::ScaleChords(Scale scale, Tuning tuning, int key) : scale_(scale), tuning_(tuning) {
  int edo = edoStepsFor(tuning);
  auto just = justScale(scale);
  if (!chords::hasChords(scale) || edo <= 0 || !just) return;
  edo_ = edo;
  key_ = ((max(key, 0) % edo) + edo) % edo;
  held_.assign(static_cast<size_t>(edo), -1);
  vector<double> error(static_cast<size_t>(edo), 0.0);
  for (size_t i = 0; i < just->gamut.size(); i++) {
    auto & note = just->gamut[i];
    if (!just->contains(note)) continue;
    int steps = note.ratio.steps(edo);
    auto step = static_cast<size_t>(((steps % edo) + edo) % edo);
    double off = fabs(note.ratio.cents() - steps * 1200.0 / edo);
    if (held_[step] >= 0 && off >= error[step] - 1e-9) continue;
    held_[step] = static_cast<int>(i);
    error[step] = off;
  }
}

optional<size_t> ScaleChords::degreeOf(int note_value) const {
  if (edo_ <= 0 || note_value < 0) return nullopt;
  auto step = static_cast<size_t>((((note_value - key_) % edo_) + edo_) % edo_);
  if (held_[step] < 0) return nullopt;
  return static_cast<size_t>(held_[step]);
}

int ScaleChords::stepOf(size_t gamut_index) const {
  if (edo_ <= 0) return 0;
  return ((ratioOf(gamut_index).steps(edo_) % edo_) + edo_) % edo_;
}

vector<const Entry *> ScaleChords::chordsOf(size_t gamut_index) const {
  vector<const Entry *> out;
  if (edo_ <= 0) return out;
  for (auto & entry : degreeChords(scale_, gamut_index)) {
    bool held = true;
    for (auto note : entry.notes) held = held && held_[static_cast<size_t>(stepOf(note))] == static_cast<int>(note);
    if (held) out.push_back(&entry);
  }
  return out;
}

string ScaleChords::noteName(size_t gamut_index) const {
  if (key_ == 0 || edo_ <= 0) return string(gamut::kExtendedDiamond[gamut_index].spelling);
  return Note::keyToString(tuning_, (key_ + stepOf(gamut_index)) % edo_);
}

string ScaleChords::chordName(const Entry & entry) const {
  auto name = noteName(entry.root) + " " + kindName(entry.kind);
  if (entry.root != entry.notes[0]) name += " / " + noteName(entry.notes[0]);
  return name;
}

Result complete(const ScaleChords & scale, vector<int> given, int size) {
  Result result;
  if (!scale.hasChords()) return result;
  sort(given.begin(), given.end());
  given.erase(unique(given.begin(), given.end()), given.end());
  result.status = Status::NOT_A_DEGREE;
  if (given.empty()) return result;
  auto degree = scale.degreeOf(given[0]);
  if (!degree) return result;
  result.status = Status::NOTHING_TO_ADD;
  if (static_cast<int>(given.size()) >= size) return result;
  result.status = Status::NO_CHORD;

  vector<size_t> given_notes; // distinct, the degree first
  for (auto value : given) {
    auto note = scale.degreeOf(value);
    if (!note) return result;
    if (find(given_notes.begin(), given_notes.end(), *note) == given_notes.end()) given_notes.push_back(*note);
  }

  for (auto entry : scale.chordsOf(*degree)) {
    auto in_entry = [&](size_t note) { return find(entry->notes.begin(), entry->notes.end(), note) != entry->notes.end(); };
    if (!all_of(given_notes.begin(), given_notes.end(), in_entry)) continue;

    Completion chord;
    chord.notes = given;
    auto written = given_notes;
    for (auto note : entry->notes) {
      if (static_cast<int>(chord.notes.size()) >= size) break;
      if (find(written.begin(), written.end(), note) != written.end()) continue;
      int above = (((scale.stepOf(note) - scale.stepOf(*degree)) % scale.edo()) + scale.edo()) % scale.edo();
      chord.notes.push_back(given[0] + above);
      written.push_back(note);
    }
    sort(chord.notes.begin(), chord.notes.end());
    bool repeat = any_of(result.chords.begin(), result.chords.end(), [&](const Completion & c) { return c.notes == chord.notes; });
    if (repeat) continue;

    chord.name = scale.chordName(*entry);
    if (written.size() < entry->notes.size()) {
      optional<string> triad;
      if (written.size() == 3) {
        auto above = aboveRoot(written, *degree);
        triad = triadName(above[0], above[1]);
      }
      if (triad)
        chord.name = scale.noteName(*degree) + " " + *triad;
      else
        chord.name += " (" + to_string(chord.notes.size()) + " notes)";
    }
    result.chords.push_back(std::move(chord));
  }
  if (!result.chords.empty()) result.status = Status::OK;
  return result;
}

} // namespace chords
