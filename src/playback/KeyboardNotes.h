#ifndef _KEYBOARDNOTES_H_
#define _KEYBOARDNOTES_H_

#include "../model/Tuning.h"
#include "../model/Scale.h"

#include <cellui/Input.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The extended diamond's keys, each a ratio of the gamut (several ratios share a
// name, so a key says which): the otonal scale runs along the q row and on along
// the a row, each utonal note on the outer row next to the otonal one it lies just
// below (the number row above the q row, the z row below the a row), the tonic of
// the next octave on 9. The names are those in the key of C.
struct ExtendedDiamondKey {
  char key;
  int num, den;
  std::string_view name;
  int octaves;
};
inline constexpr ExtendedDiamondKey kExtendedDiamondKeys[] = {
    {'q', 1, 1, "C", 0},
    {'w', 49, 48, "D𝄫", 0},
    {'2', 50, 49, "D𝄫", 0},
    {'e', 15, 14, "D♭↓", 0},
    {'3', 16, 15, "D♭↓", 0},
    {'r', 9, 8, "D", 0},
    {'4', 10, 9, "D↓", 0},
    {'t', 7, 6, "D♯", 0},
    {'5', 8, 7, "E𝄫", 0},
    {'y', 49, 40, "D𝄪", 0},
    {'6', 6, 5, "E♭", 0},
    {'u', 5, 4, "E", 0},
    {'7', 60, 49, "F𝄫", 0},
    {'i', 9, 7, "F♭", 0},
    {'o', 4, 3, "F", 0},
    {'p', 7, 5, "F♯↑", 0},
    {'0', 48, 35, "G𝄫", 0},
    {'s', 35, 24, "F𝄪", 0},
    {'z', 10, 7, "G♭↓", 0},
    {'d', 3, 2, "G", 0},
    {'x', 14, 9, "G♯", 0},
    {'f', 49, 30, "G𝄪", 0},
    {'c', 8, 5, "A♭", 0},
    {'g', 5, 3, "A", 0},
    {'v', 80, 49, "A♭↑", 0},
    {'h', 7, 4, "A♯↑", 0},
    {'b', 12, 7, "B𝄫", 0},
    {'j', 9, 5, "B♭", 0},
    {'n', 16, 9, "B♭↓", 0},
    {'k', 15, 8, "B", 0},
    {'m', 28, 15, "B", 0},
    {'l', 49, 25, "B♯", 0},
    {',', 96, 49, "B♯", 0},
    {'9', 1, 1, "C'", 1},
};

inline const ExtendedDiamondKey * extendedDiamondKey(int key) {
  for (auto & entry : kExtendedDiamondKeys) {
    if (entry.key == key) return &entry;
  }
  return nullptr;
}

// The note-entry keyboard mapping: the MIDI note a key plays in a tuning, or -1.
inline const std::string_view keyToMidiNote(int key, Tuning tuning, Scale scale) {
  if (tuning == Tuning::PERCUSSION) {
    // The 4x4 drum rack of the Launchpad: General MIDI notes 36-51, left to
    // right and then bottom to top, on the four key rows x.., s.., w.., 2.. (not a, the note-off key)
    switch (key) {
      case 'x':
        return "BD"; // 36 Bass Drum
      case 'c':
        return "RS"; // 37 Side Stick
      case 'v':
        return "SD"; // 38 Acoustic Snare
      case 'b':
        return "CP"; // 39 Hand Clap
      case 's':
        return "SD2"; // 40 Electric Snare
      case 'd':
        return "TFL"; // 41 Low Floor Tom
      case 'f':
        return "CH"; // 42 Closed Hi-hat
      case 'g':
        return "TFH"; // 43 High Floor Tom
      case 'w':
        return "PH"; // 44 Pedal Hi-hat
      case 'e':
        return "LT"; // 45 Low Tom
      case 'r':
        return "OH"; // 46 Open Hi-hat
      case 't':
        return "MT"; // 47 Low-Mid Tom
      case '2':
        return "MT2"; // 48 Hi-Mid Tom
      case '3':
        return "CR"; // 49 Crash Cymbal 1
      case '4':
        return "HT"; // 50 High Tom
      case '5':
        return "RD"; // 51 Ride Cymbal 1
    }
  } else if (isBabylonianMode(scale) || scale == Scale::RAST) {
    // The scale's degrees on q w e r t y u i o p in ascending order, then the
    // tonic and second an octave up on the keys left over (Phrygian tonos has
    // ten degrees and none left); the spellings are the gamut's own (Rāst: its
    // quarter-tone ones).
    static const std::map<Scale, std::vector<std::string>> table = [] {
      std::map<Scale, std::vector<std::string>> out;
      for (auto mode : {Scale::NIS_TUHRIM, Scale::ISARTUM, Scale::EMBUBUM, Scale::NID_QABLIM, Scale::QABLITUM, Scale::KITMUM, Scale::PITUM, Scale::PHRYGIAN_TONOS, Scale::RAST}) {
        auto & keys = out[mode];
        for (auto name : scaleDegreeNames(mode)) keys.emplace_back(name);
        if (keys.size() < 10) {
          keys.push_back(keys[0] + "'");
          keys.push_back(keys[1] + "'");
        }
      }
      return out;
    }();
    static constexpr std::string_view kKeys = "qwertyuiop";
    auto at = key >= 0 && key < 128 ? kKeys.find(static_cast<char>(key)) : std::string_view::npos;
    if (at != std::string_view::npos && at < table.at(scale).size()) return table.at(scale)[at];
  } else if (scale == Scale::EXTENDED_DIAMOND) {
    // The 33 ratios, each on its key (kExtendedDiamondKeys); the digits 1 and 8 and the letter a stay free.
    if (auto entry = extendedDiamondKey(key)) return entry->name;
  } else if (scale == Scale::DIAMOND) {
    // The thirteen notes of the 7-limit diamond on two rows like a piano: the otonal
    // ones on the q row, the utonal ones on the digit row, each on the digit between the
    // keys of its two neighbours in pitch. Read along the rows they rise q 2 w 3 e r t 6
    // y 7 u 8 i; o is the tonic of the next octave.
    switch (key) {
      case 'q':
        return "C";
      case '2':
        return "E𝄫";
      case 'w':
        return "D♯";
      case '3':
        return "E♭";
      case 'e':
        return "E";
      case 'r':
        return "F";
      case 't':
        return "F♯↑";
      case '6':
        return "G♭↓";
      case 'y':
        return "G";
      case '7':
        return "A♭";
      case 'u':
        return "A";
      case '8':
        return "B𝄫";
      case 'i':
        return "A♯↑";
      case 'o':
        return "C'";
    }
  } else if (scale == Scale::OTONAL || scale == Scale::UTONAL) {
    // The scale's eighteen notes. The row q w e r t y u has the seven of the
    // core, then the tonic and two more notes an octave up on i o p. The number
    // row, staggered above, has the notes that lie between neighbouring core
    // notes: 2 D (D↓), 3 D𝄪, 4 F♭ (F𝄫), 5 the tritone, 6 G𝄪 (G♯), 7 A♭↑ (utonal),
    // 8 B♭ (B♭↓). The notes clustered around the tonic and the tritone are on
    // 1 (D𝄫), the home row below (s D♭↓; f G𝄫 (utonal); g F𝄪 (otonal)) and, by
    // the octave, k B and l B♯. The two layouts mirror each other.
    const bool otonal = scale == Scale::OTONAL;
    switch (key) {
      case 'q':
        return "C";
      case '1':
        return "D𝄫";
      case 's':
        return "D♭↓";
      case '2':
        return otonal ? "D" : "D↓";
      case 'w':
        return otonal ? "D♯" : "E𝄫";
      case '3':
        return otonal ? "D𝄪" : "";
      case 'e':
        return otonal ? "E" : "E♭";
      case '4':
        return otonal ? "F♭" : "F𝄫";
      case 'r':
        return "F";
      case 'f':
        return otonal ? "" : "G𝄫";
      case '5':
        return otonal ? "F♯↑" : "G♭↓";
      case 'g':
        return otonal ? "F𝄪" : "";
      case 't':
        return "G";
      case '6':
        return otonal ? "G𝄪" : "G♯";
      case 'y':
        return otonal ? "A" : "A♭";
      case '7':
        return otonal ? "" : "A♭↑";
      case 'u':
        return otonal ? "A♯↑" : "B𝄫";
      case '8':
        return otonal ? "B♭" : "B♭↓";
      case 'k':
        return "B";
      case 'l':
        return "B♯";
      case 'i':
        return "C'";
      case 'o':
        return otonal ? "D♯'" : "E𝄫'";
      case 'p':
        return otonal ? "E'" : "E♭'";
    }
  } else {
    // A scale with no layout of its own plays this one (the layout the earlier
    // otonal scale had).
    switch (key) {
      case 'z':
        return "C";
      case 'w':
        return "D♭";
      case 'e':
        return "D";
      case 'x':
        return "D♯";
      case 'c':
        return "E";
      case 'v':
        return "F";
      case 'b':
        return "G";
      case 'n':
        return "A";
      case 'm':
        return "A♯";
      case 'o':
        return "C𝄫'";
      case 'p':
        return "B";
      case ',':
        return "C'";
      case '.':
        return "D♯'";
      case '-':
        return "E'";

        // The utonal notes are also available
      case 's':
        return "E𝄫";
      case 'd':
        return "E♭";
      case 'h':
        return "A♭";
      case 'j':
        return "B𝄫";
      case 'l':
        return "E𝄫";
    }
  }
  return "";
}

inline const std::string_view keyToMidiNote(const cellui::Input & input, Tuning tuning, Scale scale) {
  return keyToMidiNote(input.getId(), tuning, scale);
}

// Whether a scale's layout gives ratios of its gamut (keyToNote()) rather than
// fixed notes. The layout is written as the notes of the key of C; each key
// stands for the ratio of the note it names there.
inline bool keyboardGivesRatios(Scale scale) {
  return scale == Scale::OTONAL || scale == Scale::UTONAL || scale == Scale::DIAMOND || scale == Scale::EXTENDED_DIAMOND;
}

// What a key of such a layout plays: a gamut note and the octaves above the
// tonic's own octave (the octave marks of the layout).
struct KeyboardRatio {
  std::size_t gamut_index = 0; // into scaleGamut(scale)
  int octaves = 0;
};

inline std::optional<KeyboardRatio> keyToRatio(int key, Tuning tuning, Scale scale) {
  if (!keyboardGivesRatios(scale) || edoStepsFor(tuning) <= 0) return std::nullopt;
  KeyboardRatio out;
  if (scale == Scale::EXTENDED_DIAMOND) {
    // A key names its ratio, which the spelling does not.
    auto entry = extendedDiamondKey(key);
    if (!entry) return std::nullopt;
    out.octaves = entry->octaves;
    for (std::size_t i = 0; i < gamut::kExtendedDiamond.size(); i++) {
      if (gamut::kExtendedDiamond[i].ratio == Ratio(entry->num, entry->den)) {
        out.gamut_index = i;
        return out;
      }
    }
    return std::nullopt;
  }
  auto name = keyToMidiNote(key, tuning, scale);
  while (!name.empty() && name.back() == '\'') {
    name.remove_suffix(1);
    out.octaves++;
  }
  if (name.empty()) return std::nullopt;
  auto found = justScale(scale);
  if (!found) return std::nullopt;
  const JustScale just = *found;
  for (std::size_t i = 0; i < just.gamut.size(); i++) {
    if (just.gamut[i].spelling == name && just.contains(just.gamut[i])) {
      out.gamut_index = i;
      return out;
    }
  }
  return std::nullopt;
}

// A key's note in a song: the tonic's key moved by the ratio's steps, and the
// correction (cents) that brings it to the ratio exactly. In an equal tuning
// the correction is the same in every song key.
struct KeyboardNote {
  int value = 0;
  std::size_t gamut_index = 0;
  double correction_cents = 0;
};

// The cents from the nearest step of the tuning to the gamut note's ratio.
inline double ratioCorrectionCents(Tuning tuning, Scale scale, std::size_t gamut_index) {
  int edo = edoStepsFor(tuning);
  const auto & ratio = scaleGamut(scale)[gamut_index].ratio;
  return ratio.cents() - ratio.steps(edo) * 1200.0 / edo;
}

// `song_key` is the song's key (only its pitch class counts; -1 reads as C); `octave` is the
// keyboard's octave, counted from the tonic: the tonic of that octave is the
// tonic's pitch class in the octave Note() numbers from C.
inline std::optional<KeyboardNote> keyToNote(int key, Tuning tuning, Scale scale, int song_key, int octave) {
  auto found = keyToRatio(key, tuning, scale);
  if (!found) return std::nullopt;
  const KeyboardRatio ratio = *found;
  int edo = edoStepsFor(tuning);
  int tonic = (octave + 1) * edo + (std::max(song_key, 0) % edo); // -1 reads as C
  KeyboardNote out;
  out.gamut_index = ratio.gamut_index;
  out.value = tonic + (ratio.octaves * edo) + scaleGamut(scale)[ratio.gamut_index].ratio.steps(edo);
  out.correction_cents = ratioCorrectionCents(tuning, scale, ratio.gamut_index);
  return out;
}

#endif
