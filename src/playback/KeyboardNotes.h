#ifndef _KEYBOARDNOTES_H_
#define _KEYBOARDNOTES_H_

#include "../model/Tuning.h"
#include "../model/Scale.h"

#include <cellui/Input.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

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
  } else if (isBabylonianMode(scale)) {
    // The scale's degrees on z x c v b n m, then the tonic and second
    // an octave up (Phrygian tonos: its ten degrees fill the row, with
    // none left over); the spellings are the gamut's own.
    static const std::map<Scale, std::vector<std::string>> table = [] {
      std::map<Scale, std::vector<std::string>> out;
      for (auto mode : {Scale::NIS_TUHRIM, Scale::ISARTUM, Scale::EMBUBUM, Scale::NID_QABLIM, Scale::QABLITUM, Scale::KITMUM, Scale::PITUM, Scale::PHRYGIAN_TONOS}) {
        auto & keys = out[mode];
        for (auto name : scaleDegreeNames(mode)) keys.emplace_back(name);
        if (keys.size() < 10) {
          keys.push_back(keys[0] + "'");
          keys.push_back(keys[1] + "'");
        }
      }
      return out;
    }();
    static constexpr std::string_view kKeys = "zxcvbnm,./";
    auto at = key >= 0 && key < 128 ? kKeys.find(static_cast<char>(key)) : std::string_view::npos;
    if (at != std::string_view::npos && at < table.at(scale).size()) return table.at(scale)[at];
  } else if (scale == Scale::RAST) {
    switch (key) {
      case 'z':
        return "C";
      case 'x':
        return "D";
      case 'c':
        return "E𝄳";
      case 'v':
        return "F";
      case 'b':
        return "G";
      case 'n':
        return "A";
      case 'm':
        return "B𝄳";
      case ',':
        return "C'";
      case '.':
        return "D'";
    }
  } else if (scale == Scale::OTONAL || scale == Scale::UTONAL) {
    // The scale's eighteen notes. The lower row z x c v b n m has the seven of
    // the core and the home row, staggered between them, the notes that lie
    // between neighbouring core notes: s D (D↓), d D𝄪, f F♭ (F𝄫), g the tritone,
    // h G𝄪 (G♯), j A♭↑ (utonal), k B♭ (B♭↓). The upper row has the notes
    // clustered around the tonic and the tritone: q D𝄫, w D♭↓, y F𝄪 (t G𝄫),
    // and o B and p B♯. The two layouts mirror each other; the tonic and two
    // more notes an octave up are on , . and -.
    const bool otonal = scale == Scale::OTONAL;
    switch (key) {
      case 'z':
        return "C";
      case 'q':
        return "D𝄫";
      case 'w':
        return "D♭↓";
      case 's':
        return otonal ? "D" : "D↓";
      case 'x':
        return otonal ? "D♯" : "E𝄫";
      case 'd':
        return otonal ? "D𝄪" : "";
      case 'c':
        return otonal ? "E" : "E♭";
      case 'f':
        return otonal ? "F♭" : "F𝄫";
      case 'v':
        return "F";
      case 't':
        return otonal ? "" : "G𝄫";
      case 'g':
        return otonal ? "F♯↑" : "G♭↓";
      case 'y':
        return otonal ? "F𝄪" : "";
      case 'b':
        return "G";
      case 'h':
        return otonal ? "G𝄪" : "G♯";
      case 'n':
        return otonal ? "A" : "A♭";
      case 'j':
        return otonal ? "" : "A♭↑";
      case 'm':
        return otonal ? "A♯↑" : "B𝄫";
      case 'k':
        return otonal ? "B♭" : "B♭↓";
      case 'o':
        return "B";
      case 'p':
        return "B♯";
      case ',':
        return "C'";
      case '.':
        return otonal ? "D♯'" : "E𝄫'";
      case '-':
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

#endif
