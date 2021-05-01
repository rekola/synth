#ifndef _NOTE_H_
#define _NOTE_H_

#include "Tuning.h"

#include <cmath>
#include <string>
#include <cassert>

class Note {
 public:  
  explicit Note() : value(-1), velocity(0x3f) { }
  explicit Note(int _value, short _velocity = 0x3f) : value(_value), velocity(_velocity) { }
  explicit Note(std::string input_value, short _velocity = 0x3f, Tuning tuning = Tuning::TET12) : velocity(_velocity) {
    replace(input_value, "♯", "#");
    replace(input_value, "♭", "b");
    replace(input_value, "𝄫", "bb");
    replace(input_value, "𝄪", "x");
    
    if (tuning == Tuning::TET12) {
      char letter = input_value[0];
      char accidental = input_value[1];

      int octave = stoi(input_value.substr(2));
      value = (octave + 1) * 12;
      
      // C C# D D# E F F# G G# A A# B

      assert(letter >= 'A' && letter <= 'G');
      if (letter >= 'C' && letter <= 'E') value += (letter - 'C') * 2;
      else if (letter == 'F' || letter == 'G') value += 5 + (letter - 'F') * 2;
      else if (letter == 'A' || letter == 'B') value += 9 + (letter - 'A') * 2;

      assert(accidental == '#' || accidental == 'b' || accidental == '-');
      if (accidental == '#') value++;
      else if (accidental == 'b') value--;
    } else if (tuning == Tuning::TET19) {
      // C C♯ D♭ D D♯ E♭ E E♯ F♭ F F♯ G♭ G G♯ A♭ A A♯ B♭ B B♯ C♭
      assert(0);
      value = 0;
    } else {
      assert(0);
      value = 0;
    }
  }

  short getValue() const { return value; }
  short getVelocity() const { return velocity; }
  float getVelocityAsFloat() const { return (float)velocity / 0x3f; }
  bool isDefined() const { return value >= 0; }
  bool isOff() const { return value == 0 || velocity == 0; }

  static void inline replace(std::string & data, const std::string from, std::string to) {
    std::string::size_type pos = 0;
    while ( 1 ) {
      pos = data.find(from, pos);
      if (pos == std::string::npos) break;
      data.replace(pos, from.size(), to);
      pos += to.size();
    }
  }

  static inline std::string keyToString(Tuning tuning, int value) {
    static const char * note_names_31tet[] = { "C", "D𝄫", "C♯", "D♭", "C𝄪", "D", "E𝄫", "D♯",
						      "E♭", "D𝄪", "E", "F♭", "E♯", "F", "G𝄫", "F♯",
						      "G♭", "F𝄪", "G", "A𝄫", "G♯", "A♭", "G𝄪", "A",
						      "B𝄫", "A♯", "B♭", "A𝄪", "B", "C♭", "B♯" };
    static const char * note_names_19tet[] = { "C", "C♯", "D♭", "D", "D♯", "E♭", "E", "E♯", "F♭", "F", "F♯",
						      "G♭", "G", "G♯", "A♭", "A", "A♯", "B♭", "B", "B♯", "C♭" };
    static const char * note_names_12tet[] = { "C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B" };
    
    if (tuning == Tuning::TET31) {
      return note_names_31tet[value % 31];
    } else if (tuning == Tuning::TET19) {
      return note_names_19tet[value % 19];
    } else {
      return note_names_12tet[value % 12];
    }    
  }
  
  std::string toString(Tuning tuning) const {
    if (isOff()) return "OFF";
    else if (isDefined()) {
      auto key_name = keyToString(tuning, value);
      if (key_name.size() == 1) key_name += '-';
      if (tuning == Tuning::TET31) {
	return key_name + std::to_string((value / 31) - 1);
      } else if (tuning == Tuning::TET19) {
	return key_name + std::to_string((value / 19) - 1);
      } else {
	return key_name + std::to_string((value / 12) - 1);
      }
    } else {
      return "n/a";
    }
  }
  
 private:
  int value; // sample position, note value or -1 for undefined note
  short velocity;
  // note specific tuning
};

#endif
