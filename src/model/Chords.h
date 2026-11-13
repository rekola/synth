#ifndef _CHORDS_H_
#define _CHORDS_H_

#include "Scale.h"
#include "Tuning.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// The chords of the otonal and utonal scales: each degree's ordered list, the
// names, and the completion of played notes to a chord. No other scale has
// chords. Notes are identified by their ratio (an index into the extended
// diamond, gamut::kExtendedDiamond), never by name: four spellings name a
// different ratio in each scale.
namespace chords {

enum class Kind { HARMONIC_SEVENTH,
                  MIRROR_SEVENTH,
                  MAJOR_SEVENTH,
                  SEVEN_SHARP_NINE,
                  MAJOR,
                  MINOR };

struct Entry {
  std::vector<std::size_t> notes; // gamut indices, upward from the degree (notes[0])
  Kind kind = Kind::MAJOR;
  std::size_t root = 0; // the gamut note the chord is named after
};

inline bool hasChords(Scale scale) { return scale == Scale::OTONAL || scale == Scale::UTONAL; }

// A degree's chords, in order; empty for a note that is no degree of the scale.
const std::vector<Entry> & degreeChords(Scale scale, std::size_t gamut_index);

// The name of a triad whose other two notes are these ratios above its lowest
// (both within the octave, ascending), if it has one.
std::optional<std::string> triadName(Ratio second, Ratio third);

// The song's scale as its tuning holds it. A scale note is held when its
// nearest step is its own; of two notes sharing a step the nearer has it, and
// a chord with the other is not offered.
class ScaleChords {
 public:
  // `key` is a note value of which only the pitch class counts (-1 reads as C).
  ScaleChords(Scale scale, Tuning tuning, int key);

  bool hasChords() const { return edo_ > 0; }
  int edo() const { return edo_; }
  // The scale note a note value is, if any.
  std::optional<std::size_t> degreeOf(int note_value) const;
  // Steps above the tonic, 0 to edo - 1.
  int stepOf(std::size_t gamut_index) const;
  // The degree's chords whose notes the tuning all holds.
  std::vector<const Entry *> chordsOf(std::size_t gamut_index) const;
  // A scale note's name, without an octave: the gamut's spelling on C, the
  // tuning's name for its key elsewhere.
  std::string noteName(std::size_t gamut_index) const;
  // "C harmonic seventh", "F harmonic seventh / D♯".
  std::string chordName(const Entry & entry) const;

 private:
  Scale scale_;
  Tuning tuning_;
  int edo_ = 0;
  int key_ = 0;
  std::vector<int> held_; // by step: gamut index, or -1
};

struct Completion {
  std::vector<int> notes; // note values, rising
  std::string name;
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
// notes. The lowest given note is the degree; every chord of its list that
// contains the others, octaves ignored, gives one result: the given notes at
// their own octaves, and the chord's other notes, from its lowest upward,
// within the octave above the lowest given note, until there are `size`
// notes. A chord with fewer notes is not padded.
Result complete(const ScaleChords & scale, std::vector<int> given, int size);

} // namespace chords

#endif
