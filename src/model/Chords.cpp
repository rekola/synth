#include "Chords.h"
#include "Note.h"
#include "Division.h"

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
  Direction direction; // built from its lowest note, or from its top note
};

const vector<Form> & forms() {
  static const vector<Form> table = {
      {Kind::HARMONIC_SEVENTH, {Ratio{5, 4}, Ratio{3, 2}, Ratio{7, 4}}, Direction::UPWARD}, // 4:5:6:7
      {Kind::MIRROR_SEVENTH, {Ratio{7, 6}, Ratio{7, 5}, Ratio{7, 4}}, Direction::DOWNWARD}, // 1/7:1/6:1/5:1/4
      {Kind::MAJOR_SEVENTH, {Ratio{5, 4}, Ratio{3, 2}, Ratio{15, 8}}, Direction::UPWARD},   // 8:10:12:15
      {Kind::SEVEN_SHARP_NINE, {Ratio{7, 6}, Ratio{5, 4}, Ratio{7, 4}}, Direction::UPWARD}, // 12:14:15:21
      {Kind::MAJOR, {Ratio{5, 4}, Ratio{3, 2}}, Direction::UPWARD},
      {Kind::MINOR, {Ratio{6, 5}, Ratio{3, 2}}, Direction::UPWARD},
      // The ninth reduced into the octave, so 9/4 is 9/8 and sorts first.
      {Kind::HARMONIC_NINTH, {Ratio{9, 8}, Ratio{5, 4}, Ratio{3, 2}, Ratio{7, 4}}, Direction::UPWARD},                  // 4:5:6:7:9
      {Kind::MIRROR_HARMONIC_NINTH, {Ratio{9, 8}, Ratio{9, 7}, Ratio{3, 2}, Ratio{9, 5}}, Direction::DOWNWARD},         // 1/9:1/7:1/6:1/5:1/4
      {Kind::MAJOR_NINTH, {Ratio{9, 8}, Ratio{5, 4}, Ratio{3, 2}, Ratio{15, 8}}, Direction::UPWARD},                    // 8:10:12:15:18
      {Kind::MIRROR_MAJOR_NINTH, {Ratio{9, 8}, Ratio{6, 5}, Ratio{3, 2}, Ratio{9, 5}}, Direction::DOWNWARD},            // 1/18:1/15:1/12:1/10:1/8
      {Kind::SEVEN_SHARP_NINE_FIFTH, {Ratio{7, 6}, Ratio{5, 4}, Ratio{3, 2}, Ratio{7, 4}}, Direction::UPWARD},          // 12:14:15:18:21
      {Kind::MIRROR_SEVEN_SHARP_NINE_FIFTH, {Ratio{7, 6}, Ratio{7, 5}, Ratio{3, 2}, Ratio{7, 4}}, Direction::DOWNWARD}, // 1/21:1/18:1/15:1/14:1/12
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

// Whether the gamut note is one `scale` borrows: a note of a chord the division gives it, that the
// scale does not have.
bool borrowedNote(const JustScale & just, size_t index) {
  if (!just.side || !just.only.empty()) return false;
  auto & note = gamut::kExtendedDiamond[index];
  return !just.contains(note) && division::borrows(*just.side, note.ratio);
}

// The chords of a scale that have a borrowed note: every chord of every form whose notes are all
// notes of the scale or borrowed, each read upward from each of its notes.
ChordTable buildBorrowedTable(Scale scale, bool five) {
  ChordTable table;
  auto just = justScale(scale);
  if (!just) return table;
  forEachChord(five, [&](Kind kind, const vector<size_t> & notes, size_t root) {
    bool borrowed = false;
    for (auto note : notes) {
      if (borrowedNote(*just, note))
        borrowed = true;
      else if (!just->contains(gamut::kExtendedDiamond[note]))
        return;
    }
    if (!borrowed) return;
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

// The note a chord is heard as: the lowest note of an upward chord in its basic form, the top
// note of a downward one.
optional<size_t> namedBy(const Entry & entry) {
  // The minor triad is entered by the subharmonic run from its top note, so no note names it.
  if (entry.kind == Kind::MINOR) return nullopt;
  const Form * form = nullptr;
  for (auto & candidate : forms()) {
    if (candidate.kind == entry.kind) form = &candidate;
  }
  if (!form) return nullopt;
  for (auto lowest : entry.notes) {
    if (aboveRoot(entry.notes, lowest) != form->above) continue;
    if (form->direction == Direction::UPWARD) return lowest;
    auto top = (ratioOf(lowest) * form->above.back()).reduced();
    for (auto note : entry.notes) {
      if (ratioOf(note) == top) return note;
    }
  }
  return nullopt;
}

// Each list starts with the chords its note names: the seventh chords (those of the direction
// the scale is named for first), the triads, then the rest of them; the other chords follow in
// their order.
ChordTable ordered(const ChordTable & table, Direction preferred) {
  ChordTable out;
  for (auto & [degree, list] : table) {
    vector<Entry> sevenths_preferred, sevenths_other, triads, named, rest;
    for (auto & entry : list) {
      auto by = namedBy(entry);
      if (!by || *by != degree) {
        rest.push_back(entry);
      } else if (entry.kind == Kind::HARMONIC_SEVENTH || entry.kind == Kind::MIRROR_SEVENTH) {
        (directionOf(entry.kind) == preferred ? sevenths_preferred : sevenths_other).push_back(entry);
      } else if (entry.kind == Kind::MAJOR || entry.kind == Kind::MINOR) {
        triads.push_back(entry);
      } else {
        named.push_back(entry);
      }
    }
    auto & result = out[degree];
    for (auto * part : {&sevenths_preferred, &sevenths_other, &triads, &named, &rest}) result.insert(result.end(), part->begin(), part->end());
  }
  return out;
}

// The chords of notes up to four (size 4), or of exactly five.
const ChordTable & tableFor(Scale scale, int size, bool borrowed = false) {
  static const ChordTable otonal_lists = buildTable(kOtonalChords, GamutSide::OTONAL);
  static const ChordTable utonal_lists = buildTable(kUtonalChords, GamutSide::UTONAL);
  static const ChordTable otonal5_lists = buildTable(kOtonalNinths, GamutSide::OTONAL);
  static const ChordTable utonal5_lists = buildTable(kUtonalNinths, GamutSide::UTONAL);
  static const ChordTable cross = buildCrossTable(false);
  static const ChordTable cross5 = buildCrossTable(true);
  static const ChordTable otonal = ordered(otonal_lists, Direction::UPWARD);
  static const ChordTable utonal = ordered(utonal_lists, Direction::DOWNWARD);
  static const ChordTable otonal5 = ordered(otonal5_lists, Direction::UPWARD);
  static const ChordTable utonal5 = ordered(utonal5_lists, Direction::DOWNWARD);
  static const ChordTable diamond = ordered(joinTables(otonal_lists, utonal_lists, cross), Direction::UPWARD);
  static const ChordTable diamond5 = ordered(joinTables(otonal5_lists, utonal5_lists, cross5), Direction::UPWARD);
  // With the borrowed chords: the scale's own lists, then the chords that borrow, ordered as ever.
  static const ChordTable otonal_b = ordered(joinTables(otonal_lists, buildBorrowedTable(Scale::OTONAL, false), {}), Direction::UPWARD);
  static const ChordTable utonal_b = ordered(joinTables(utonal_lists, buildBorrowedTable(Scale::UTONAL, false), {}), Direction::DOWNWARD);
  static const ChordTable otonal5_b = ordered(joinTables(otonal5_lists, buildBorrowedTable(Scale::OTONAL, true), {}), Direction::UPWARD);
  static const ChordTable utonal5_b = ordered(joinTables(utonal5_lists, buildBorrowedTable(Scale::UTONAL, true), {}), Direction::DOWNWARD);
  static const ChordTable none;
  bool five = size >= 5;
  if (borrowed) {
    if (scale == Scale::OTONAL) return five ? otonal5_b : otonal_b;
    if (scale == Scale::UTONAL) return five ? utonal5_b : utonal_b;
  }
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

Direction directionOf(Kind kind) {
  for (auto & form : forms()) {
    if (form.kind == kind) return form.direction;
  }
  return Direction::UPWARD;
}

const vector<Entry> & degreeChords(Scale scale, size_t gamut_index, int size, bool borrowed) {
  static const vector<Entry> none;
  auto & table = tableFor(scale, size, borrowed);
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

ScaleChords::ScaleChords(Scale scale, Tuning tuning, int key, bool borrowed) : scale_(scale), tuning_(tuning), borrowed_(borrowed && (scale == Scale::OTONAL || scale == Scale::UTONAL)) {
  int edo = edoStepsFor(tuning);
  auto just = justScale(scale);
  if (!chords::hasChords(scale) || edo <= 0 || !just) return;
  edo_ = edo;
  key_ = ((max(key, 0) % edo) + edo) % edo;
  held_.assign(static_cast<size_t>(edo), -1);
  vector<double> error(static_cast<size_t>(edo), 0.0);
  vector<int> rank(static_cast<size_t>(edo), 0);
  for (size_t i = 0; i < just->gamut.size(); i++) {
    auto & note = just->gamut[i];
    // A note of the scale, or with the setting a borrowed one, which never takes the key of a note of the scale.
    int note_rank = just->contains(note) ? 2 : (borrowed_ && borrowedNote(*just, i) ? 1 : 0);
    if (note_rank == 0) continue;
    int steps = note.ratio.steps(edo);
    auto step = static_cast<size_t>(((steps % edo) + edo) % edo);
    double off = fabs(note.ratio.cents() - steps * 1200.0 / edo);
    if (held_[step] >= 0 && (note_rank < rank[step] || (note_rank == rank[step] && off >= error[step] - 1e-9))) continue;
    held_[step] = static_cast<int>(i);
    rank[step] = note_rank;
    error[step] = off;
  }
}

bool ScaleChords::isBorrowed(size_t gamut_index) const {
  auto just = justScale(scale_);
  return borrowed_ && just && borrowedNote(*just, gamut_index);
}

optional<size_t> ScaleChords::degreeOf(int note_value) const {
  if (edo_ <= 0 || note_value < 0) return nullopt;
  auto step = static_cast<size_t>((((note_value - key_) % edo_) + edo_) % edo_);
  if (held_[step] < 0) return nullopt;
  return static_cast<size_t>(held_[step]);
}

bool ScaleChords::holds(size_t gamut_index) const {
  return edo_ > 0 && held_[static_cast<size_t>(stepOf(gamut_index))] == static_cast<int>(gamut_index);
}

int ScaleChords::stepOf(size_t gamut_index) const {
  if (edo_ <= 0) return 0;
  return ((ratioOf(gamut_index).steps(edo_) % edo_) + edo_) % edo_;
}

vector<const Entry *> ScaleChords::chordsOf(size_t gamut_index, int size) const {
  vector<const Entry *> out;
  if (edo_ <= 0) return out;
  for (auto & entry : degreeChords(scale_, gamut_index, size, borrowed_)) {
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
  // An upward candidate is found from the lowest played note, a downward one from the highest.
  auto low_degree = ratio_of(given.front());
  if (!low_degree) return result;
  result.status = Status::NOTHING_TO_ADD;
  if (static_cast<int>(given.size()) >= size) return result;
  result.status = Status::NO_CHORD;

  vector<size_t> given_notes; // distinct
  for (auto value : given) {
    auto note = ratio_of(value);
    if (!note) return result;
    if (find(given_notes.begin(), given_notes.end(), *note) == given_notes.end()) given_notes.push_back(*note);
  }
  const bool single = given.size() == 1;
  auto high_degree = ratio_of(given.back()); // a note of the scale: the loop above would have returned
  if (!high_degree) return result;

  auto add = [&](const Entry * entry) {
    auto in_entry = [&](size_t note) { return find(entry->notes.begin(), entry->notes.end(), note) != entry->notes.end(); };
    if (!all_of(given_notes.begin(), given_notes.end(), in_entry)) return;
    const bool downward = directionOf(entry->kind) == Direction::DOWNWARD;

    Completion chord;
    vector<pair<int, size_t>> placed; // value and ratio
    for (auto value : given) placed.emplace_back(value, *ratio_of(value));
    auto written = given_notes;
    if (downward) {
      // A downward chord hangs from its top note; nothing is added above it. One played note
      // has the top an octave above it, since the pitch heard in a mirror chord follows its
      // lowest note; several played notes keep their octaves, the highest being the top.
      // The others are added below, the nearest first, so a chord cut to the width keeps the
      // notes just below the top.
      const size_t top_note = single ? *low_degree : *high_degree;
      const int top = single ? given.front() + scale.edo() : given.back();
      if (single) {
        placed.clear();
        placed.emplace_back(top, top_note);
      }
      vector<pair<int, size_t>> below; // steps below the top note, note
      for (auto note : entry->notes) {
        if (find(written.begin(), written.end(), note) != written.end()) continue;
        int steps = (((scale.stepOf(top_note) - scale.stepOf(note)) % scale.edo()) + scale.edo()) % scale.edo();
        below.emplace_back(steps, note);
      }
      sort(below.begin(), below.end());
      for (auto & [steps, note] : below) {
        if (static_cast<int>(placed.size()) >= size) break;
        placed.emplace_back(top - steps, note);
        written.push_back(note);
      }
      // A note below the lowest the track can hold: the chord is not offered.
      if (any_of(placed.begin(), placed.end(), [](const pair<int, size_t> & note) { return note.first < 0; })) return;
    } else {
      for (auto note : entry->notes) {
        if (static_cast<int>(placed.size()) >= size) break;
        if (find(written.begin(), written.end(), note) != written.end()) continue;
        int above = (((scale.stepOf(note) - scale.stepOf(*low_degree)) % scale.edo()) + scale.edo()) % scale.edo();
        placed.emplace_back(given.front() + above, note);
        written.push_back(note);
      }
    }
    sort(placed.begin(), placed.end());
    // A triad is named on its lowest note.
    auto lowest = placed.front().second;
    // A seventh chord at five or more columns repeats its first note an octave on: higher in an
    // upward chord, lower in a downward one.
    if (size >= 5 && placed.size() == 4 && entry->notes.size() == 4) {
      auto & first = downward ? placed.back() : placed.front();
      int value = downward ? first.first - scale.edo() : first.first + scale.edo();
      bool taken = any_of(placed.begin(), placed.end(), [&](const pair<int, size_t> & note) { return note.first == value; });
      if (value >= 0 && !taken) {
        if (downward)
          placed.insert(placed.begin(), {value, first.second});
        else
          placed.emplace_back(value, first.second);
      }
    }
    // The first column holds the played note: an upward chord rises from it, a downward one falls.
    if (downward) reverse(placed.begin(), placed.end());
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
        auto above = aboveRoot(written, lowest);
        triad = triadName(above[0], above[1]);
      }
      if (triad)
        chord.name = scale.noteName(lowest) + " " + *triad;
      else
        chord.name += " (" + to_string(chord.notes.size()) + " notes)";
    }
    result.chords.push_back(std::move(chord));
  };
  // The candidates of a list size. One played note has its own list, each chord read in its
  // direction. Several played notes take the upward chords of the lowest note's list and the
  // downward chords of the highest note's, those of the direction the scale is named for first.
  auto candidates = [&](int list) {
    vector<const Entry *> out;
    if (single) return scale.chordsOf(*low_degree, list);
    vector<const Entry *> up, down;
    for (auto entry : scale.chordsOf(*low_degree, list)) {
      if (directionOf(entry->kind) == Direction::UPWARD) up.push_back(entry);
    }
    for (auto entry : scale.chordsOf(*high_degree, list)) {
      if (directionOf(entry->kind) == Direction::DOWNWARD) down.push_back(entry);
    }

    bool upward_first = scale.lists() != Scale::UTONAL;
    out = upward_first ? up : down;
    out.insert(out.end(), upward_first ? down.begin() : up.begin(), upward_first ? down.end() : up.end());
    return out;
  };
  // A note's own chord is a run of consecutive ratios from 4: its notes times 5/4, 6/4, 7/4, 8/4 and
  // 9/4 (harmonic) or its top note times 4/5 ... 4/9 (subharmonic); the width says how many are written.
  // The run stops at the first ratio the scale lacks, and a note names one if it has at least 4:5:6.
  // Only for one played note; a subharmonic chord hangs from the note an octave above it.
  auto run = [&](Direction direction, int count) {
    vector<pair<int, size_t>> out;
    const int base = direction == Direction::UPWARD ? given.front() : given.front() + scale.edo();
    out.emplace_back(base, *low_degree);
    for (int k = 5; k <= 9 && static_cast<int>(out.size()) < count; k++) {
      auto ratio = (direction == Direction::UPWARD ? ratioOf(*low_degree) * Ratio{k, 4} : ratioOf(*low_degree) * Ratio{4, k}).reduced();
      optional<size_t> note;
      for (size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
        if (ratioOf(i) == ratio) note = i;
      }
      if (!note || !scale.holds(*note)) break;
      int octaves = k >= 8 ? 1 : 0;
      int offset = direction == Direction::UPWARD ? scale.stepOf(*note) - scale.stepOf(*low_degree) : scale.stepOf(*low_degree) - scale.stepOf(*note);
      offset = ((offset % scale.edo()) + scale.edo()) % scale.edo() + octaves * scale.edo();
      int value = direction == Direction::UPWARD ? base + offset : base - offset;
      if (value < 0) break;
      out.emplace_back(value, *note);
    }
    return out;
  };
  if (single) {
    const Direction preferred = scale.lists() == Scale::UTONAL ? Direction::DOWNWARD : Direction::UPWARD;
    const Direction other = preferred == Direction::UPWARD ? Direction::DOWNWARD : Direction::UPWARD;
    size_t preferred_length = run(preferred, kMaxSize).size();
    size_t other_length = run(other, kMaxSize).size();
    // The longer run comes first; if they are equally long, the kind the scale is named for.
    vector<Direction> order;
    if (other_length > preferred_length)
      order = {other, preferred};
    else
      order = {preferred, other};
    auto emit_run = [&](Direction direction) {
      auto notes = run(direction, size);
      Completion chord;
      for (auto & [value, note] : notes) {
        chord.notes.push_back(value);
        chord.ratios.push_back(note);
      }
      if (any_of(result.chords.begin(), result.chords.end(), [&](const Completion & c) { return c.notes == chord.notes && c.ratios == chord.ratios; })) return;
      const bool up = direction == Direction::UPWARD;
      const string root = scale.noteName(*low_degree);
      const string family = up ? "harmonic" : "mirror";
      switch (notes.size()) {
        case 2:
          chord.name = root + " " + family + " seventh (2 notes)";
          break;
        case 3: {
          // A triad is named on its lowest note: C major, F minor.
          const size_t lowest_note = up ? notes.front().second : notes.back().second;
          vector<size_t> members;
          for (auto & [value, note] : notes) members.push_back(note);
          auto above = aboveRoot(members, lowest_note);
          auto triad = triadName(above[0], above[1]);
          chord.name = triad ? scale.noteName(lowest_note) + " " + *triad : root + " " + family + " seventh (3 notes)";
          break;
        }
        case 4:
        case 5:
          chord.name = root + " " + family + " seventh";
          break;
        default:
          chord.name = root + " " + family + " ninth";
          break;
      }
      result.chords.push_back(std::move(chord));
    };
    auto length_of = [&](Direction direction) { return direction == preferred ? preferred_length : other_length; };
    // The width chooses the class of chord: a triad, a seventh chord (four and five columns) or a
    // ninth chord. In each class the note's own run comes first if it reaches the class, then the
    // first chord of the class in the note's list; a ninth class falls back to the seventh class.
    struct Class {
      size_t run_length;
      int list;
    };
    vector<Class> classes;
    if (size >= 6)
      classes = {{6, 5}, {4, 4}};
    else if (size >= 4)
      classes = {{4, 4}};
    else
      classes = {{3, 0}};
    for (auto & klass : classes) {
      for (auto direction : order) {
        if (length_of(direction) >= klass.run_length) emit_run(direction);
      }
      if (klass.list == 0) continue;
      for (auto entry : scale.chordsOf(*low_degree, klass.list)) {
        if (static_cast<int>(entry->notes.size()) == klass.list) {
          add(entry);
          break;
        }
      }
    }
    // The other runs and chords follow as alternatives.
    for (auto direction : order) {
      if (length_of(direction) >= 3) emit_run(direction);
    }
  }
  // Ninth chords come first at a width of six or more, then the seventh chords; at five columns
  // the seventh chords are first and the ninth chords follow.
  if (size >= 6) {
    for (auto entry : candidates(5)) add(entry);
    for (auto entry : candidates(4)) add(entry);
  } else {
    for (auto entry : candidates(4)) add(entry);
    if (size >= 5) {
      for (auto entry : candidates(5)) add(entry);
    }
  }
  // A chord with a borrowed note says so.
  for (auto & chord : result.chords) {
    chord.borrowed = any_of(chord.ratios.begin(), chord.ratios.end(), [&](size_t note) { return scale.isBorrowed(note); });
    if (chord.borrowed) chord.name += " (borrowed)";
  }
  if (!result.chords.empty()) result.status = Status::OK;
  return result;
}

} // namespace chords
