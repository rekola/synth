#ifndef _NOTE_H_
#define _NOTE_H_

#include "Tuning.h"

#include <cmath>
#include <string>
#include <cassert>

class Note {
 public:
  explicit Note() : note_number(0), velocity(0x3f) { }
  explicit Note(int _note_number, short _velocity = 0x3f) : note_number(_note_number), velocity(_velocity) { }
  explicit Note(std::string value, short _velocity = 0x3f, Tuning tuning = Tuning::TET12) : velocity(_velocity) {
    if (tuning == Tuning::TET12) {
      char letter = value[0];
      char accidental = value[1];

      int octave = stoi(value.substr(2));
      note_number = (octave + 1) * 12;
      
      // C C# D D# E F F# G G# A A# B

      assert(letter >= 'A' && letter <= 'G');
      if (letter >= 'C' && letter <= 'E') note_number += (letter - 'C') * 2;
      else if (letter == 'F' || letter == 'G') note_number += 5 + (letter - 'F') * 2;
      else if (letter == 'A' || letter == 'B') note_number += 9 + (letter - 'A') * 2;

      assert(accidental == '#' || accidental == 'b' || accidental == '-');
      if (accidental == '#') note_number++;
      else if (accidental == 'b') note_number--;
    } else {
      assert(0);
      note_number = 0;
    }
  }

  short getNoteNumber() const { return note_number; }
  short getVelocity() const { return velocity; }
  float getVelocityAsFloat() const { return (float)velocity / 0x3f; }
  bool isDefined() const { return note_number > 0; }
  bool isOff() const { return note_number == 1 || velocity == 0; }
  
  inline float getFrequency(Tuning tuning, int transpose, int detune) {
    if (tuning == Tuning::TET31) {
      return 440.0f * powf(2.0f, (note_number - 178.0f + transpose + detune / 100.0f) / 31.0f);
    } else if (tuning == Tuning::TET12) {
      return 440.0f * powf(2.0f, (note_number - 69.0f + transpose + detune / 100.0f) / 12.0f);
    } else {
      assert(0);
    }
  }

  std::string toString(Tuning tuning) const {
    if (isOff()) return "OFF";
    else if (note_number > 1) {
      static const char * note_names_31tet[] = { "C-", "D𝄫", "C♯", "D♭", "C𝄪", "D-", "E𝄫", "D♯",
						 "E♭", "D𝄪", "E-", "F♭", "E♯", "F-", "G𝄫", "F♯",
						 "G♭", "F𝄪", "G-", "A𝄫", "G♯", "A♭", "G𝄪", "A-",
						 "B𝄫", "A♯", "B♭", "A𝄪", "B-", "C♭", "B♯" };
      static const char * note_names_12tet[] = { "C-", "C♯", "D-", "D♯", "E-", "F-", "F♯", "G-", "G♯", "A-", "A♯", "B-" };
      
      if (tuning == Tuning::TET31) {
	return std::string(note_names_31tet[note_number % 31]) + std::to_string((note_number / 31) - 1);
      } else {
	return std::string(note_names_12tet[note_number % 12]) + std::to_string((note_number / 12) - 1);
      }
    } else {
      return "???";
    }
  }
  
 private:
  short note_number, velocity;
  // note specific tuning
};

#endif
