#ifndef _NOTE_H_
#define _NOTE_H_

#include "Tuning.h"

#include <string>
#include <string_view>
#include <cassert>
#include <charconv>

class Note {
 public:  
  Note() : value(-1), velocity(0), delay(0) { }
  explicit Note(int _value, short _velocity = 0x28, short _delay = 0) : value(_value), velocity(_velocity), delay(_delay) { }
  explicit Note(std::string_view input_value, short _velocity = 0x28, short _delay = 0, Tuning tuning = Tuning::TET12)
    : value(stringToKey(tuning, std::move(input_value))),
      velocity(_velocity),
      delay(_delay) { }

  short getValue() const { return value; }
  short getVelocity() const { return velocity; }
  float getVelocityAsFloat() const { return velocity / 127.0f; }
  short getDelay() const { return delay; }
  float getDelayAsFloat() const { return delay / 255.0f; }
  bool isDefined() const { return value >= 0 || velocity > 0; }
  bool isOff() const { return value >= 0 && velocity == 0; }
  bool isAftertouch() const { return value == -1 && velocity > 0; }

  void setVelocity(short v) { velocity = v; }
  void setDelay(short d) { delay = d; }

  void clear() {
    value = -1;
    velocity = 0;
    delay = 0;
  }

  void transposeUp() {
    if (isDefined() && !isOff() && !isAftertouch()) value++;
  }
  void transposeDown() {
    if (isDefined() && !isOff() && !isAftertouch()) value--;
  }

  std::string toString(Tuning tuning) const {
    if (isDefined() && !isAftertouch()) {
      if (isOff()) {
	return "OFF";
      } else {
	auto key_name = keyToString(tuning, getValue());
	if (tuning == Tuning::PERCUSSION) {
	  key_name += ' ';
	} else {
	  if (key_name.size() == 1) key_name += '-';
	  if (tuning == Tuning::TET31) {
	    key_name += std::to_string((getValue() / 31) - 1);
	  } else if (tuning == Tuning::TET19) {
	    key_name += std::to_string((getValue() / 19) - 1);
	  } else {
	    key_name += std::to_string((getValue() / 12) - 1);
	  }
	}
	return key_name;
      }
    } else {
      return "···";
    }
  }

  static inline std::string keyToString(Tuning tuning, int value) {
    static const char * note_names_31tet[] = { "C", "D𝄫", "C♯", "D♭", "C𝄪", "D", "E𝄫", "D♯",
					       "E♭", "D𝄪", "E", "F♭", "E♯", "F", "G𝄫", "F♯",
					       "G♭", "F𝄪", "G", "A𝄫", "G♯", "A♭", "G𝄪", "A",
					       "B𝄫", "A♯", "B♭", "A𝄪", "B", "C♭", "B♯" };
    static const char * note_names_19tet[] = { "C", "C♯", "D♭", "D", "D♯", "E♭", "E", "E♯", "F", "F♯",
					       "G♭", "G", "G♯", "A♭", "A", "A♯", "B♭", "B", "C♭" };
    static const char * note_names_12tet[] = { "C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B" };

    static const char * percussion_names[] = { "HighQ", "Slap", "Stratch Push", "Stratch Pull", "Sticks", "Square Click", "Metr.Click", "Metr.Bell", "BD", "eBD", "Side Stick", "SD", "Hand Clap", "eSnare", "Low Floor Tom", "CH", "High Floor Tom", "HF", "T4", "OH", "T3", "T2", "CC1", "T1", "RC1", "Chinese Cymbal", "Ride Bell", "TA", "SC", "CB", "CC2", "Vibra Slap", "RC2", "B1", "B2", "Mute High Conga", "Open High Conga", "Low Conga", "High Timbale", "Low Timbale", "High Agogô", "Low Agogô", "Cabasa", "Maracas", "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro", "Claves", "WB1", "WB2", "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle", "SH" };

    if (tuning == Tuning::PERCUSSION) {
      if (value >= 27 && value <= 82) return percussion_names[value - 27];
      return "#" + std::to_string(value);
    } else if (tuning == Tuning::TET31) {
      return note_names_31tet[value % 31];
    } else if (tuning == Tuning::TET19) {
      return note_names_19tet[value % 19];
    } else {
      return note_names_12tet[value % 12];
    }    
  }

  static inline int stringToKey(Tuning tuning, std::string_view input_value) {
    if (input_value == "off" || input_value == "OFF") {
      return 0;
    } else if (tuning == Tuning::PERCUSSION) {
      if (input_value == "BD") return 35; // Acoustic Base Drum
      else if (input_value == "SD") return 38; // Acoustic Snare (or SN)
      else if (input_value == "F2") return 41; // Low Floor Tom
      else if (input_value == "CH") return 42; // Closed Hi-hat
      else if (input_value == "F1") return 43; // High Floor Tom
      else if (input_value == "HF") return 44; // Pedal Hi-hat
      else if (input_value == "T4") return 45; // Low Tom
      else if (input_value == "OH") return 46; // Open Hi-hat
      else if (input_value == "T3") return 47; // Low-Mid Tom
      else if (input_value == "T2") return 48; // Hi-Mid Tom
      else if (input_value == "CC1") return 49; // Crash Cymbal 1
      else if (input_value == "T1") return 50; // High Tom
      else if (input_value == "RC1") return 51; // Ride Cymbal 1
      else if (input_value == "TA") return 54; // Tambourine
      else if (input_value == "SC") return 55; // Splash Cymbal
      else if (input_value == "CB") return 56; // Cowbell
      else if (input_value == "CC2") return 57; // Crash Cymbal 2
      else if (input_value == "RC2") return 59; // Ride Cymbal 2
      else if (input_value == "B1") return 60; // High Bongo
      else if (input_value == "B2") return 61; // Low Bongo
      else if (input_value == "WB1") return 76; // High Woodblock
      else if (input_value == "WB2") return 77; // Low Woodblock
      else if (input_value == "SH") return 82; // Shaker
      else return 0;
    } else {     
      int octave = 4;
      auto pos = input_value.find_first_of("0123456789");
      if (pos != std::string_view::npos) {
	auto octave_text = input_value.substr(pos);
	auto result = std::from_chars(octave_text.data(), octave_text.data() + octave_text.size(), octave);
	if (result.ec == std::errc::invalid_argument) return 0;
	input_value.remove_suffix(octave_text.size());
      }
      auto letter = input_value[0];
      auto accidental = input_value.substr(1);

      assert(letter >= 'A' && letter <= 'G');
      
      if (tuning == Tuning::TET12) {
	auto value = (octave + 1) * 12;
      
	// C C# D D# E F F# G G# A A# B

	if (letter >= 'C' && letter <= 'E') value += (letter - 'C') * 2;
	else if (letter == 'F' || letter == 'G') value += 5 + (letter - 'F') * 2;
	else if (letter == 'A' || letter == 'B') value += 9 + (letter - 'A') * 2;
	else {
	  assert(0);
	}

	if (accidental == "#" || accidental == "♯") value++;
	else if (accidental == "b" || accidental == "♭") value--;
	else {
	  assert(accidental == "-");
	}

	return value;
      } else if (tuning == Tuning::TET19) {
	auto value = (octave + 1) * 19;

	// C C♯ D♭ D D♯ E♭ E E♯ F♭ F F♯ G♭ G G♯ A♭ A A♯ B♭ B B♯ C♭
	assert(0);
	return value;
      } else {
	auto value = (octave + 1) * 31;
      
	// C D𝄫 C♯ D♭ C𝄪 D E𝄫 D♯
	// E♭ D𝄪/F𝄫 E F♭ E♯ F E𝄪/G𝄫 F♯
	// G♭ F𝄪 G A𝄫 G♯ A♭ G𝄪 A
	// B𝄫 A♯ B♭ A𝄪 B C♭ B♯

	if (letter >= 'C' && letter <= 'E') value += (letter - 'C') * 5;
	else if (letter >= 'F' && letter <= 'G') value += 13 + (letter - 'F') * 5;
	else if (letter >= 'A' && letter <= 'B') value += 23 + (letter - 'A') * 5;
	else {
	  assert(0);
	}

	if (accidental == "#" || accidental == "♯") value += 2;
	else if (accidental == "b" || accidental == "♭") value -= 2;
	else if (accidental == "x" || accidental == "𝄪") value += 4;
	else if (accidental == "bb" || accidental == "𝄫") value -= 4;
	else {
	  assert(accidental.empty() || accidental == "-");
	}

	return value;
      }
    }
  }
    
 private:
  int value; // sample position, note value or -1 for undefined note
  short velocity;
  short delay;  
};

#endif
