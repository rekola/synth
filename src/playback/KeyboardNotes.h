#ifndef _KEYBOARDNOTES_H_
#define _KEYBOARDNOTES_H_

#include "../instruments/Tuning.h"
#include "../model/Scale.h"

#include <cellui/Input.h>

#include <string_view>

// The note-entry keyboard mapping: the MIDI note a key plays in a tuning, or -1.
inline const std::string_view keyToMidiNote(const cellui::Input & input, Tuning tuning, Scale scale) {
  if (tuning == Tuning::PERCUSSION) {
    switch (input.getId()) {
      case '1':
        return "HQ";
      case '2':
        return "SLP"; // Slap
      case '3':
        return "SPU"; // Stratch Push
      case '4':
        return "SPD"; // Stratch Pull
      case '5':
        return "STK"; // Sticks
      case '6':
        return "SQC"; // Square Click
      case '7':
        return "MCK"; // Metronome Click
      case '8':
        return "MBL"; // Metronome Bell
      case '9':
        return "BD2"; // Acoustic Bass Drum
      case '0':
        return "BD"; // Electric Bass Drum
      case 'q':
        return "RS"; // Side Stick
      case 'w':
        return "SD"; // Acoustic Snare
      case 'e':
        return "CP"; // Hand Clap
      case 'r':
        return "SD2"; // Electric Snare
      case 't':
        return "TFL"; // Low Floor Tom
      case 'y':
        return "CH"; // Closed Hi-hat
      case 'u':
        return "TFH"; // High Floor Tom
      case 'i':
        return "PH"; // Pedal Hi-hat
      case 'o':
        return "LT"; // Low Tom
      case 'p':
        return "OH"; // Open Hi-hat
      case 's':
        return "MT"; // Low-Mid Tom
      case 'd':
        return "MT2"; // Hi-Mid Tom
      case 'f':
        return "CR"; // Crash Cymbal 1
      case 'g':
        return "HT"; // High Tom
      case 'h':
        return "RD"; // Ride Cymbal 1
      case 'j':
        return "CHN"; // Chinese Cymbal
      case 'k':
        return "RB"; // Ride Bell
      case 'l':
        return "TMB"; // Tambourine
      case 'z':
        return "SPL"; // Splash Cymbal
      case 'x':
        return "CB"; // Cowbell
      case 'c':
        return "CR2"; // Crash Cymbal 2
      case 'v':
        return "VS"; // Vibra Slap
      case 'b':
        return "RD2"; // Ride Cymbal 2
      case 'n':
        return "BOH"; // High Bongo
      case 'm':
        return "BOL"; // Low Bongo
      case '.':
        return "CGM"; // Mute High Conga
      case ',':
        return "CGH"; // Open High Conga
      case '-':
        return "CGL"; // Low Conga
                      // 65 High Timbale
	// 66 Low Timbale
	// 67 High Agogô
	// 68 Low Agogô
	// 69 Cabasa
	// 70 Maracas
	// 71 Short Whistle
	// 72 Long Whistle
	// 73 Short Guiro
	// 74 Long Guiro
	// 75 Claves
	// 76 High Woodblock
	// 77 Low Woodblock
	// 78 Mute Cuica
	// 79 Open Cuica
	// 80 Mute Triangle
	// 81 Open Triangle
	// 82 Shaker
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
