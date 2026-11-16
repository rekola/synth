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

// Five-note chords, offered at a width of five. Same order rule per degree: chords
// from a lower step first, harmonic ninths and their mirrors, major ninths and
// theirs, 7♯9 chords and theirs, then the chord named after the degree itself.
constexpr array kOtonalNinths = {
    "C D♯ F G A",
    "C E F G A",
    "C D♯ E G A♯↑",
    "C D E G A♯↑",
    "C D F♭ G B♭",
    "C D E G B",
    "C E G A B",
    "C D♯ F♯↑ G A♯↑",
    "D♭↓ E G A B",
    "D E G A♯↑ C",
    "D F♭ G B♭ C",
    "D E G B C",
    "D♯ F G A C",
    "D♯ E G A♯↑ C",
    "D♯ F♯↑ G A♯↑ C",
    "E F G A C",
    "E G A♯↑ C D♯",
    "E G A♯↑ C D",
    "E G A B D♭↓",
    "E G B C D",
    "E G A B C",
    "F♭ G B♭ C D",
    "F G A C D♯",
    "F G A C E",
    "F♯↑ G A♯↑ C D♯",
    "G A C D♯ F",
    "G A C E F",
    "G A♯↑ C D♯ E",
    "G A♯↑ C D E",
    "G A B D♭↓ E",
    "G B♭ C D F♭",
    "G B C D E",
    "G A B C E",
    "G A♯↑ C D♯ F♯↑",
    "A C D♯ F G",
    "A C E F G",
    "A B D♭↓ E G",
    "A B C E G",
    "A♯↑ C D♯ E G",
    "A♯↑ C D E G",
    "A♯↑ C D♯ F♯↑ G",
    "B♭ C D F♭ G",
    "B D♭↓ E G A",
    "B C D E G",
    "B C E G A",
};

constexpr array kUtonalNinths = {
    "C E♭ F G B𝄫",
    "C E♭ F G A♭",
    "C E𝄫 F A♭ B𝄫",
    "C D↓ F G♯ B♭↓",
    "C E𝄫 F A♭ B♭↓",
    "C D♭↓ E♭ F A♭",
    "C D♭↓ F A♭ B♭↓",
    "C E𝄫 F G♭↓ B𝄫",
    "D♭↓ E♭ F A♭ B",
    "D♭↓ E♭ F A♭ C",
    "D♭↓ F A♭ B♭↓ C",
    "D↓ F G♯ B♭↓ C",
    "E𝄫 F A♭ B𝄫 C",
    "E𝄫 F A♭ B♭↓ C",
    "E𝄫 F G♭↓ B𝄫 C",
    "E♭ F G B𝄫 C",
    "E♭ F G A♭ C",
    "E♭ F A♭ B D♭↓",
    "E♭ F A♭ C D♭↓",
    "F G B𝄫 C E♭",
    "F G A♭ C E♭",
    "F A♭ B𝄫 C E𝄫",
    "F A♭ B D♭↓ E♭",
    "F G♯ B♭↓ C D↓",
    "F A♭ B♭↓ C E𝄫",
    "F A♭ C D♭↓ E♭",
    "F A♭ B♭↓ C D♭↓",
    "F G♭↓ B𝄫 C E𝄫",
    "G♭↓ B𝄫 C E𝄫 F",
    "G B𝄫 C E♭ F",
    "G A♭ C E♭ F",
    "G♯ B♭↓ C D↓ F",
    "A♭ C E♭ F G",
    "A♭ B𝄫 C E𝄫 F",
    "A♭ B D♭↓ E♭ F",
    "A♭ B♭↓ C E𝄫 F",
    "A♭ C D♭↓ E♭ F",
    "A♭ B♭↓ C D♭↓ F",
    "B𝄫 C E♭ F G",
    "B𝄫 C E𝄫 F A♭",
    "B𝄫 C E𝄫 F G♭↓",
    "B♭↓ C D↓ F G♯",
    "B♭↓ C E𝄫 F A♭",
    "B♭↓ C D♭↓ F A♭",
    "B D♭↓ E♭ F A♭",
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
      // The ninth reduced into the octave, so 9/4 is 9/8 and sorts first.
      {Kind::HARMONIC_NINTH, {Ratio{9, 8}, Ratio{5, 4}, Ratio{3, 2}, Ratio{7, 4}}},                // 4:5:6:7:9
      {Kind::MIRROR_HARMONIC_NINTH, {Ratio{9, 8}, Ratio{9, 7}, Ratio{3, 2}, Ratio{9, 5}}},         // 1/9:1/7:1/6:1/5:1/4
      {Kind::MAJOR_NINTH, {Ratio{9, 8}, Ratio{5, 4}, Ratio{3, 2}, Ratio{15, 8}}},                  // 8:10:12:15:18
      {Kind::MIRROR_MAJOR_NINTH, {Ratio{9, 8}, Ratio{6, 5}, Ratio{3, 2}, Ratio{9, 5}}},            // 1/18:1/15:1/12:1/10:1/8
      {Kind::SEVEN_SHARP_NINE_FIFTH, {Ratio{7, 6}, Ratio{5, 4}, Ratio{3, 2}, Ratio{7, 4}}},        // 12:14:15:18:21
      {Kind::MIRROR_SEVEN_SHARP_NINE_FIFTH, {Ratio{7, 6}, Ratio{7, 5}, Ratio{3, 2}, Ratio{7, 4}}}, // 1/21:1/18:1/15:1/14:1/12
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
    case Kind::HARMONIC_NINTH:
      return "harmonic ninth";
    case Kind::MIRROR_HARMONIC_NINTH:
      return "mirror harmonic ninth";
    case Kind::MAJOR_NINTH:
      return "major ninth";
    case Kind::MIRROR_MAJOR_NINTH:
      return "mirror major ninth";
    case Kind::SEVEN_SHARP_NINE_FIFTH:
      return "7♯9 with fifth";
    case Kind::MIRROR_SEVEN_SHARP_NINE_FIFTH:
      return "mirror 7♯9 with fifth";
  }
  return "";
}

const Ratio & ratioOf(size_t index) { return gamut::kExtendedDiamond[index].ratio; }

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

// The note a chord is named after: the lowest of its form, except a mirror
// seventh, which is named by its top note.
size_t rootOf(Kind kind, const vector<size_t> & notes, size_t lowest) {
  if (kind != Kind::MIRROR_SEVENTH) return lowest;
  size_t top = lowest;
  Ratio highest{1, 1};
  for (auto note : notes) {
    auto above = (ratioOf(note) / ratioOf(lowest)).reduced();
    if (above > highest) {
      highest = above;
      top = note;
    }
  }
  return top;
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
        entry.root = rootOf(form.kind, entry.notes, root);
        named = true;
      }
    }
    if (named) table[entry.notes[0]].push_back(std::move(entry));
  }
  return table;
}

bool hasSide(const vector<size_t> & notes, GamutSide side) {
  return any_of(notes.begin(), notes.end(), [&](size_t i) { return gamut::kExtendedDiamond[i].side == side; });
}

// The kinds of chord, in the order the lists offer them (seventh chords, triads,
// then the major sevenths and the 7♯9 chords; the five-note kinds as the enum has them).
vector<Kind> kindOrder(bool five) {
  vector<Kind> order;
  if (five) {
    for (auto & form : forms()) {
      if (form.above.size() == 4) order.push_back(form.kind);
    }
  } else {
    order = {Kind::HARMONIC_SEVENTH, Kind::MIRROR_SEVENTH, Kind::MAJOR, Kind::MINOR, Kind::MAJOR_SEVENTH, Kind::SEVEN_SHARP_NINE};
  }
  return order;
}

// Calls `use` with each chord of every form (by kind, then by ascending lowest
// note) whose notes are all ratios of the gamut: the notes by gamut index, the
// lowest of the form first, and the chord's root.
template <class Use>
void forEachChord(bool five, Use use) {
  for (auto kind : kindOrder(five)) {
    const Form * form = nullptr;
    for (auto & candidate : forms()) {
      if (candidate.kind == kind) form = &candidate;
    }
    for (size_t lowest = 0; lowest < gamut::kExtendedDiamond.size(); lowest++) {
      vector<size_t> notes = {lowest};
      for (auto & above : form->above) {
        auto ratio = (ratioOf(lowest) * above).reduced();
        for (size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
          if (ratioOf(i) == ratio) {
            notes.push_back(i);
            break;
          }
        }
      }
      if (notes.size() == form->above.size() + 1) use(kind, notes, rootOf(kind, notes, lowest));
    }
  }
}

// The chord read upward from one of its notes.
Entry readFrom(Kind kind, size_t root, const vector<size_t> & notes, size_t degree) {
  Entry entry;
  entry.kind = kind;
  entry.root = root;
  entry.notes = {degree};
  vector<pair<Ratio, size_t>> upward;
  for (auto note : notes) {
    if (note != degree) upward.emplace_back((ratioOf(note) / ratioOf(degree)).reduced(), note);
  }
  sort(upward.begin(), upward.end());
  for (auto & [ratio, note] : upward) entry.notes.push_back(note);
  return entry;
}

// Every chord of every form that has notes of both scales, each read upward from
// each of its notes, by kind and by ascending lowest note within a kind.
ChordTable buildCrossTable(bool five) {
  ChordTable table;
  forEachChord(five, [&](Kind kind, const vector<size_t> & notes, size_t root) {
    if (!hasSide(notes, GamutSide::OTONAL) || !hasSide(notes, GamutSide::UTONAL)) return;
    for (auto degree : notes) table[degree].push_back(readFrom(kind, root, notes, degree));
  });
  return table;
}

// The diamond scales' lists: each degree's otonal chords, then its utonal ones,
// then the chords that take notes from both.
ChordTable joinTables(const ChordTable & otonal, const ChordTable & utonal, const ChordTable & cross) {
  ChordTable table;
  for (auto * part : {&otonal, &utonal, &cross}) {
    for (auto & [degree, entries] : *part) table[degree].insert(table[degree].end(), entries.begin(), entries.end());
  }
  return table;
}

// The chords of notes up to four (size 4), or of exactly five.
const ChordTable & tableFor(Scale scale, int size) {
  static const ChordTable otonal = buildTable(kOtonalChords, GamutSide::OTONAL);
  static const ChordTable utonal = buildTable(kUtonalChords, GamutSide::UTONAL);
  static const ChordTable otonal5 = buildTable(kOtonalNinths, GamutSide::OTONAL);
  static const ChordTable utonal5 = buildTable(kUtonalNinths, GamutSide::UTONAL);
  static const ChordTable cross = buildCrossTable(false);
  static const ChordTable cross5 = buildCrossTable(true);
  static const ChordTable diamond = joinTables(otonal, utonal, cross);
  static const ChordTable diamond5 = joinTables(otonal5, utonal5, cross5);
  static const ChordTable none;
  bool five = size >= 5;
  switch (scale) {
    case Scale::OTONAL:
      return five ? otonal5 : otonal;
    case Scale::UTONAL:
      return five ? utonal5 : utonal;
    case Scale::DIAMOND:
      return five ? diamond5 : diamond;
    default:
      return none;
  }
}

} // namespace

const vector<Entry> & degreeChords(Scale scale, size_t gamut_index, int size) {
  static const vector<Entry> none;
  auto & table = tableFor(scale, size);
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

vector<const Entry *> ScaleChords::chordsOf(size_t gamut_index, int size) const {
  vector<const Entry *> out;
  if (edo_ <= 0) return out;
  for (auto & entry : degreeChords(scale_, gamut_index, size)) {
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

Result complete(const ScaleChords & scale, vector<int> given, int size, const map<int, size_t> & known) {
  Result result;
  if (!scale.hasChords()) return result;
  sort(given.begin(), given.end());
  given.erase(unique(given.begin(), given.end()), given.end());
  result.status = Status::NOT_A_DEGREE;
  if (given.empty()) return result;
  auto ratio_of = [&](int value) -> optional<size_t> {
    if (auto it = known.find(value); it != known.end()) return it->second;
    return scale.degreeOf(value);
  };
  auto degree = ratio_of(given[0]);
  if (!degree) return result;
  result.status = Status::NOTHING_TO_ADD;
  if (static_cast<int>(given.size()) >= size) return result;
  result.status = Status::NO_CHORD;

  vector<size_t> given_notes; // distinct, the degree first
  for (auto value : given) {
    auto note = ratio_of(value);
    if (!note) return result;
    if (find(given_notes.begin(), given_notes.end(), *note) == given_notes.end()) given_notes.push_back(*note);
  }

  auto add = [&](const Entry * entry) {
    auto in_entry = [&](size_t note) { return find(entry->notes.begin(), entry->notes.end(), note) != entry->notes.end(); };
    if (!all_of(given_notes.begin(), given_notes.end(), in_entry)) return;

    Completion chord;
    vector<pair<int, size_t>> placed; // value and ratio
    for (auto value : given) placed.emplace_back(value, *ratio_of(value));
    auto written = given_notes;
    for (auto note : entry->notes) {
      if (static_cast<int>(placed.size()) >= size) break;
      if (find(written.begin(), written.end(), note) != written.end()) continue;
      int above = (((scale.stepOf(note) - scale.stepOf(*degree)) % scale.edo()) + scale.edo()) % scale.edo();
      placed.emplace_back(given[0] + above, note);
      written.push_back(note);
    }
    sort(placed.begin(), placed.end());
    for (auto & [value, note] : placed) {
      chord.notes.push_back(value);
      chord.ratios.push_back(note);
    }
    bool repeat = any_of(result.chords.begin(), result.chords.end(), [&](const Completion & c) { return c.notes == chord.notes && c.ratios == chord.ratios; });
    if (repeat) return;

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
  };
  // The widest chords first, then each smaller list down to the four-note one.
  for (int list = min(size, kMaxSize); list >= 5; list--) {
    for (auto entry : scale.chordsOf(*degree, list)) add(entry);
  }
  for (auto entry : scale.chordsOf(*degree)) add(entry);
  if (!result.chords.empty()) result.status = Status::OK;
  return result;
}

} // namespace chords
