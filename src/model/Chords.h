#ifndef _CHORDS_H_
#define _CHORDS_H_

#include "Scale.h"
#include "Tuning.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

// The chords of the otonal, utonal and diamond scales: each degree's ordered
// list, the names, and the completion of played notes to a chord. No other scale
// has chords. Notes are identified by their ratio (an index into the extended
// diamond, gamut::kExtendedDiamond), never by name: four spellings name a
// different ratio in each scale.
namespace chords {

enum class Kind { HARMONIC_SEVENTH,
                  SUBHARMONIC_SEVENTH,
                  MAJOR_SEVENTH,
                  SEVEN_SHARP_NINE,
                  MAJOR,
                  MINOR,
                  HARMONIC_NINTH,
                  SUBHARMONIC_HARMONIC_NINTH,
                  MAJOR_NINTH,
                  SUBHARMONIC_MAJOR_NINTH,
                  SEVEN_SHARP_NINE_FIFTH,
                  SUBHARMONIC_SEVEN_SHARP_NINE_FIFTH };

// Which note a chord is built from. An upward chord (its proportion in whole
// numbers, 4:5:6:7) has its notes above its lowest note; a downward chord (the
// subharmonic form of an upward one, 1/4:1/5:1/6:1/7) has them below its top note.
enum class Direction { UPWARD,
                       DOWNWARD };

// The direction of a kind of chord: the subharmonic form of an upward kind is downward, so
// the minor triad (A C E hangs from E) is.
Direction directionOf(Kind kind);

// The most notes a chord has.
constexpr int kMaxSize = 6;

struct Entry {
  std::vector<std::size_t> notes; // gamut indices, upward from the degree (notes[0])
  Kind kind = Kind::MAJOR;
  std::size_t root = 0; // the gamut note the chord is named after
};

inline bool hasChords(Scale scale) {
  return scale == Scale::OTONAL || scale == Scale::UTONAL || scale == Scale::DIAMOND;
}

// A degree's chords, in order; empty for a note that is no degree of the scale.
// In the two diamond scales a note has the chords of its own side first (a note
// of both, the otonal ones before the utonal ones), then the chords that take
// notes from both.
// `size` 4 (or less) is the list of chords of two to four notes, 5 the
// five-note chords.
// With `borrowed`, otonal and utonal also have the chords that take notes from the other
// scale (division.h): every chord whose notes are all in the scale or borrowed, with at
// least one borrowed note.
const std::vector<Entry> & degreeChords(Scale scale, std::size_t gamut_index, int size = 4, bool borrowed = false);

// The name of a triad whose other two notes are these ratios above its lowest
// (both within the octave, ascending), if it has one.
std::optional<std::string> triadName(Ratio second, Ratio third);

// The song's scale as its tuning holds it. A scale note is held when its
// nearest step is its own; of two notes sharing a step the nearer has it, and
// a chord with the other is not offered.
class ScaleChords {
 public:
  // `key` is a note value of which only the pitch class counts (-1 reads as C).
  // With `borrowed` (otonal and utonal only) the notes of the division's chords that the scale lacks
  // are available as notes of chords, each on a key no note of the scale has.
  ScaleChords(Scale scale, Tuning tuning, int key, bool borrowed = false);

  bool hasChords() const { return edo_ > 0; }
  // The scale whose lists these are.
  Scale lists() const { return scale_; }
  int edo() const { return edo_; }
  // The scale note a note value is, if any.
  std::optional<std::size_t> degreeOf(int note_value) const;
  // Steps above the tonic, 0 to edo - 1.
  int stepOf(std::size_t gamut_index) const;
  // Whether the scale has this gamut note on its key.
  bool holds(std::size_t gamut_index) const;
  // Whether the gamut note is one the scale borrows (and `borrowed` is on).
  bool isBorrowed(std::size_t gamut_index) const;
  // The degree's chords whose notes the tuning all holds.
  std::vector<const Entry *> chordsOf(std::size_t gamut_index, int size = 4) const;
  // A scale note's name, without an octave: the gamut's spelling on C, the
  // tuning's name for its key elsewhere.
  std::string noteName(std::size_t gamut_index) const;
  // "C harmonic seventh", "F harmonic seventh / D♯", "F harmonic ninth / C".
  std::string chordName(const Entry & entry) const;

 private:
  Scale scale_;
  Tuning tuning_;
  int edo_ = 0;
  int key_ = 0;
  bool borrowed_ = false;
  std::vector<int> held_; // by step: gamut index, or -1
};

struct Completion {
  std::vector<int> notes; // note values, rising
  std::vector<std::size_t> ratios; // each note's gamut index, as `notes`
  std::string name;
  bool borrowed = false; // a note of it is borrowed from the other scale; the name says so
};

enum class Status {
  OK,
  NO_SCALE,       // the scale has no chords
  NOT_A_DEGREE,   // the lowest given note is not a note of the scale
  NOTHING_TO_ADD, // as many given notes as the chord may have, or more
  NO_CHORD,       // no chord of the degree contains every given note
};

struct Result {
  Status status = Status::NO_SCALE;
  // The completion first, then the alternatives; no two alike.
  std::vector<Completion> chords;
};

// Completes the given notes (note values, in any order) to chords of `size`
// notes. A downward chord is written with a single given note as its top note an
// octave above it, the others added below that (the nearest first when cut to `size`). The lowest given note is the degree; every chord of its list that
// contains the others, octaves ignored, gives one result: the given notes at
// their own octaves, and the chord's other notes, from its lowest upward,
// within the octave above the lowest given note, until there are `size`
// notes. A chord with fewer notes is not padded. Chords of `size` notes come
// first (five at most), then each smaller list down to the four-note one.
// `known` gives the ratio (gamut index) of a given note value that the caller
// knows, such as the key that played it; any other given note is the scale
// note its key holds.
Result complete(const ScaleChords & scale, std::vector<int> given, int size, const std::map<int, std::size_t> & known = {});

} // namespace chords

#endif
