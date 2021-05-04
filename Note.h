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

    int octave;
    std::string::size_type pos = input_value.find_first_of("0123456789");
    if (pos != std::string::npos) {
      octave = stoi(input_value.substr(pos));
      input_value.erase(pos);
    } else {
      octave = 4;
    }
    char letter = input_value[0];
    std::string accidental = input_value.substr(1);

    assert(letter >= 'A' && letter <= 'G');
      
    if (tuning == Tuning::TET12) {
      value = (octave + 1) * 12;
      
      // C C# D D# E F F# G G# A A# B

      if (letter >= 'C' && letter <= 'E') value += (letter - 'C') * 2;
      else if (letter == 'F' || letter == 'G') value += 5 + (letter - 'F') * 2;
      else if (letter == 'A' || letter == 'B') value += 9 + (letter - 'A') * 2;
      else {
	assert(0);
      }
      
      if (accidental == "#") value++;
      else if (accidental == "b") value--;
      else {
	assert(accidental == "-");
      }
    } else if (tuning == Tuning::TET19) {
      value = (octave + 1) * 19;

      // C C♯ D♭ D D♯ E♭ E E♯ F♭ F F♯ G♭ G G♯ A♭ A A♯ B♭ B B♯ C♭
      assert(0);
      value = 0;
    } else {
      value = (octave + 1) * 31;
      
      // C D𝄫 C♯ D♭ C𝄪 D E𝄫 D♯,
      // E♭ D𝄪 E F♭ E♯ F G𝄫 F♯,
      // G♭ F𝄪 G A𝄫 G♯ A♭ G𝄪 A,
      // B𝄫 A♯ B♭ A𝄪 B C♭ B♯

      if (letter >= 'C' && letter <= 'E') value += (letter - 'C') * 5;
      else if (letter >= 'F' && letter <= 'G') value += 13 + (letter - 'F') * 5;
      else if (letter >= 'A' && letter <= 'B') value += 23 + (letter - 'A') * 5;
      else {
	assert(0);
      }

      if (accidental == "#") value += 2;
      else if (accidental == "b") value -= 2;
      else if (accidental == "x") value += 4;
      else if (accidental == "bb") value -= 4;
      else {
	assert(accidental == "-");
      }
    }
  }

  short getValue() const { return value; }
  short getVelocity() const { return velocity; }
  float getVelocityAsFloat() const { return velocity / 127.0f; }
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

  float getPanning(Tuning tuning) const {
    if (isOff()) {
      return 0.5f;
    } else {
      float p;
      if (tuning == Tuning::TET12) {
	p = 0.5f + 0.5f * (value - 69.0f) / 12.0f;
      } else if (tuning == Tuning::TET31) {
	p = 0.5f + 0.5f * (value - 178.0f) / 31.0f;
      } else {
	return 0.5f;
      }

      if (p < 0.0f) p = 0.0f;
      else if (p > 1.0f) p = 1.0f;
      return p;
    }
  }
  
 private:
  int value; // sample position, note value or -1 for undefined note
  short velocity;
  // note specific tuning
};

#endif
