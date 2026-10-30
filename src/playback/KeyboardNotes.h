#ifndef _KEYBOARDNOTES_H_
#define _KEYBOARDNOTES_H_

#include "../instruments/Tuning.h"
#include "../model/Scale.h"

#include <cellui/Input.h>

#include <string_view>

// The note-entry keyboard mapping: the MIDI note a key plays in a tuning, or -1.
inline const std::string_view keyToMidiNote(const cellui::Input & input, Tuning tuning, Scale scale) {
  if (tuning == Tuning::PERCUSSION) {
    // The 4x4 drum rack of the Launchpad: General MIDI notes 36-51, left to
    // right and then bottom to top, on the four key rows z.., s.., w.., 2.. (not a, the note-off key)
    switch (input.getId()) {
      case 'z':
        return "BD"; // 36 Bass Drum
      case 'x':
        return "RS"; // 37 Side Stick
      case 'c':
        return "SD"; // 38 Acoustic Snare
      case 'v':
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
  } else {
    // A scale with no layout of its own plays the otonal one.
    if (scale != Scale::UTONAL) {
      switch (input.getId()) {
        case 'z':
          return "C";
        case 's':
          return "D♭";
        case 'd':
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
          return "A#";
        case 'k':
          return "C𝄫'";
        case 'l':
          return "B";
        case ',':
          return "C'";
        case '.':
          return "D#'";
        case '-':
          return "E'";
      }
    } else {
      switch (input.getId()) {
        case 'z':
          return "C";
        case 's':
          return "D𝄭";
        case 'd':
          return "C𝄪";
        case 'x':
          return "E𝄫";
        case 'c':
          return "E♭";
        case 'v':
          return "F";
        case 'b':
          return "G";
        case 'n':
          return "A♭";
        case 'm':
          return "B𝄫";
        case 'k':
          return "B𝄭";
        case 'l':
          return "B↓";
        case ',':
          return "C'";
        case '.':
          return "E𝄫'";
        case '-':
          return "E♭'";
      }
    }
  }
  return "";
}

#endif
