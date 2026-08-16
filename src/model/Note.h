#ifndef _NOTE_H_
#define _NOTE_H_

#include "../instruments/Tuning.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <cassert>
#include <charconv>
#include <vector>

class Note {
 public:  
  Note() : value(-1), velocity(0), delay(0) { }
  explicit Note(int _value, short _velocity = 0x28, short _delay = 0) : value(_value), velocity(_velocity), delay(_delay) { }
  explicit Note(std::string_view input_value, short _velocity = 0x28, short _delay = 0, Tuning tuning = Tuning::TET12)
    : value(stringToKey(tuning, std::move(input_value))),
      velocity(_velocity),
      delay(_delay) { }

  // int, not short: value is stored as int (transpose() can legitimately
  // grow it well past SHRT_MAX for a high-EDO tuning many octaves up -
  // see its own comment), and every caller already treats the result as
  // int (Tuner::getFrequency(Tuning, int)/noteOn's int note_value/...) -
  // returning short here was pure incidental narrowing on the way out,
  // not something any caller actually needed.
  int getValue() const { return value; }
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

  // `delta` may be any size (positive up, negative down) - not just ±1 -
  // so a whole-block transpose can shift by more than a semitone/step in
  // one call rather than requiring a caller-side loop of single steps.
  // value + delta is computed in int64_t specifically so an extreme delta
  // can never signed-overflow the addition itself (UB) before the clamp
  // below ever runs - INT_MAX + INT_MAX still fits comfortably in
  // int64_t. Clamped to [0, INT_MAX]: the lower bound keeps a
  // transpose-down from ever landing on -1 - isDefined()'s own
  // "undefined" sentinel - which a naive value-- at 0 would otherwise
  // silently turn this into; there's no meaningful fixed upper bound (a
  // 53-EDO value spans well past a byte-sized range once you're a few
  // octaves up), so this just stops at whatever `value` itself can hold.
  void transpose(int delta) {
    if (!isDefined() || isOff() || isAftertouch()) return;
    auto next = static_cast<int64_t>(value) + delta;
    value = static_cast<int>(std::clamp<int64_t>(next, 0, std::numeric_limits<int>::max()));
  }

  std::string toString(Tuning tuning) const {
    if (isDefined() && !isAftertouch()) {
      if (isOff()) {
	return "OFF";
      } else {
	auto key_name = keyToString(tuning, getValue());
	if (tuning != Tuning::PERCUSSION) {
	  if (key_name.size() == 1) key_name += '-';
	  if (tuning == Tuning::TET53) {
	    key_name += std::to_string((getValue() / 53) - 1);
	  } else if (tuning == Tuning::TET31) {
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
    static const char * note_names_53edo[] = {
      "C", "C𝄮", "D𝄫", "C♯", "C𝄰", "D𝄭", "D♭", "C𝄪", "D𝄯", "D", "E𝄫", "D𝄱", "D♯", "D𝄰",
      "E♭", "F𝄫", "D𝄪", "E", "E𝄮", "F♭", "E♯", "F𝄯", "F", "F𝄮", "E𝄪", "F♯", "F𝄰",
      "G𝄭", "G♭", "F𝄪", "G𝄯", "G", "A𝄫", "G𝄱", "G♯", "A𝄭", "A♭", "A𝄬", "G𝄪", "A",
      "A𝄮", "B𝄫", "A♯", "A𝄰", "B𝄭", "B♭", "A𝄪", "B𝄯", "B", "B𝄮", "B𝄱", "B♯", "B𝄰"
    };
    static const char * note_names_31edo[] = { "C", "D𝄫", "C♯", "D♭", "C𝄪", "D", "E𝄫", "D♯",
					       "E♭", "D𝄪", "E", "F♭", "E♯", "F", "G𝄫", "F♯",
					       "G♭", "F𝄪", "G", "A𝄫", "G♯", "A♭", "G𝄪", "A",
					       "B𝄫", "A♯", "B♭", "A𝄪", "B", "C♭", "B♯" };
    static const char * note_names_19edo[] = { "C", "C♯", "D♭", "D", "D♯", "E♭", "E", "E♯", "F", "F♯",
					       "G♭", "G", "G♯", "A♭", "A", "A♯", "B♭", "B", "C♭" };
    static const char * note_names_12edo[] = { "C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B" };

    // One short (1-3 letter) drum-notation-style abbreviation per GM
    // percussion key (27-82) - see SoundFont.cpp's PAN table, which shares
    // this exact indexing convention.
    static const char * percussion_names[] = {
      "HQ",   // 27 High Q
      "SLP",  // 28 Slap
      "SPU",  // 29 Scratch Push
      "SPD",  // 30 Scratch Pull
      "STK",  // 31 Sticks
      "SQC",  // 32 Square Click
      "MCK",  // 33 Metronome Click
      "MBL",  // 34 Metronome Bell
      "BD2",  // 35 Acoustic Bass Drum
      "BD",   // 36 Bass Drum 1
      "RS",   // 37 Side Stick
      "SD",   // 38 Acoustic Snare
      "CP",   // 39 Hand Clap
      "SD2",  // 40 Electric Snare
      "TFL",  // 41 Low Floor Tom
      "CH",   // 42 Closed Hi-Hat
      "TFH",  // 43 High Floor Tom
      "PH",   // 44 Pedal Hi-Hat
      "LT",   // 45 Low Tom
      "OH",   // 46 Open Hi-Hat
      "MT",   // 47 Low-Mid Tom
      "MT2",  // 48 Hi-Mid Tom
      "CR",   // 49 Crash Cymbal 1
      "HT",   // 50 High Tom
      "RD",   // 51 Ride Cymbal 1
      "CHN",  // 52 Chinese Cymbal
      "RB",   // 53 Ride Bell
      "TMB",  // 54 Tambourine
      "SPL",  // 55 Splash Cymbal
      "CB",   // 56 Cowbell
      "CR2",  // 57 Crash Cymbal 2
      "VS",   // 58 Vibraslap
      "RD2",  // 59 Ride Cymbal 2
      "BOH",  // 60 High Bongo
      "BOL",  // 61 Low Bongo
      "CGM",  // 62 Mute High Conga
      "CGH",  // 63 Open High Conga
      "CGL",  // 64 Low Conga
      "TIH",  // 65 High Timbale
      "TIL",  // 66 Low Timbale
      "AGH",  // 67 High Agogô
      "AGL",  // 68 Low Agogô
      "CAB",  // 69 Cabasa
      "MA",   // 70 Maracas
      "WHS",  // 71 Short Whistle
      "WHL",  // 72 Long Whistle
      "GRS",  // 73 Short Guiro
      "GRL",  // 74 Long Guiro
      "CL",   // 75 Claves
      "WBH",  // 76 High Woodblock
      "WBL",  // 77 Low Woodblock
      "CUM",  // 78 Mute Cuica
      "CUO",  // 79 Open Cuica
      "TRM",  // 80 Mute Triangle
      "TRO",  // 81 Open Triangle
      "SH",   // 82 Shaker
    };

    if (tuning == Tuning::PERCUSSION) {
      if (value >= 27 && value <= 82) return percussion_names[value - 27];
      return "#" + std::to_string(value);
    } else if (tuning == Tuning::TET53) {
      return note_names_53edo[value % 53];
    } else if (tuning == Tuning::TET31) {
      return note_names_31edo[value % 31];
    } else if (tuning == Tuning::TET19) {
      return note_names_19edo[value % 19];
    } else {
      return note_names_12edo[value % 12];
    }    
  }

  static inline int stringToKey(Tuning tuning, std::string_view input_value) {
    if (input_value == "off" || input_value == "OFF") {
      return 0;
    } else if (tuning == Tuning::PERCUSSION) {
      if (input_value == "HQ") return 27; // High Q
      else if (input_value == "SLP") return 28; // Slap
      else if (input_value == "SPU") return 29; // Scratch Push
      else if (input_value == "SPD") return 30; // Scratch Pull
      else if (input_value == "STK") return 31; // Sticks
      else if (input_value == "SQC") return 32; // Square Click
      else if (input_value == "MCK") return 33; // Metronome Click
      else if (input_value == "MBL") return 34; // Metronome Bell
      else if (input_value == "BD2") return 35; // Acoustic Bass Drum
      else if (input_value == "BD") return 36; // Bass Drum 1
      else if (input_value == "RS") return 37; // Side Stick
      else if (input_value == "SD") return 38; // Acoustic Snare
      else if (input_value == "CP") return 39; // Hand Clap
      else if (input_value == "SD2") return 40; // Electric Snare
      else if (input_value == "TFL") return 41; // Low Floor Tom
      else if (input_value == "CH") return 42; // Closed Hi-hat
      else if (input_value == "TFH") return 43; // High Floor Tom
      else if (input_value == "PH") return 44; // Pedal Hi-hat
      else if (input_value == "LT") return 45; // Low Tom
      else if (input_value == "OH") return 46; // Open Hi-hat
      else if (input_value == "MT") return 47; // Low-Mid Tom
      else if (input_value == "MT2") return 48; // Hi-Mid Tom
      else if (input_value == "CR") return 49; // Crash Cymbal 1
      else if (input_value == "HT") return 50; // High Tom
      else if (input_value == "RD") return 51; // Ride Cymbal 1
      else if (input_value == "CHN") return 52; // Chinese Cymbal
      else if (input_value == "RB") return 53; // Ride Bell
      else if (input_value == "TMB") return 54; // Tambourine
      else if (input_value == "SPL") return 55; // Splash Cymbal
      else if (input_value == "CB") return 56; // Cowbell
      else if (input_value == "CR2") return 57; // Crash Cymbal 2
      else if (input_value == "VS") return 58; // Vibraslap
      else if (input_value == "RD2") return 59; // Ride Cymbal 2
      else if (input_value == "BOH") return 60; // High Bongo
      else if (input_value == "BOL") return 61; // Low Bongo
      else if (input_value == "CGM") return 62; // Mute High Conga
      else if (input_value == "CGH") return 63; // Open High Conga
      else if (input_value == "CGL") return 64; // Low Conga
      else if (input_value == "TIH") return 65; // High Timbale
      else if (input_value == "TIL") return 66; // Low Timbale
      else if (input_value == "AGH") return 67; // High Agogô
      else if (input_value == "AGL") return 68; // Low Agogô
      else if (input_value == "CAB") return 69; // Cabasa
      else if (input_value == "MA") return 70; // Maracas
      else if (input_value == "WHS") return 71; // Short Whistle
      else if (input_value == "WHL") return 72; // Long Whistle
      else if (input_value == "GRS") return 73; // Short Guiro
      else if (input_value == "GRL") return 74; // Long Guiro
      else if (input_value == "CL") return 75; // Claves
      else if (input_value == "WBH") return 76; // High Woodblock
      else if (input_value == "WBL") return 77; // Low Woodblock
      else if (input_value == "CUM") return 78; // Mute Cuica
      else if (input_value == "CUO") return 79; // Open Cuica
      else if (input_value == "TRM") return 80; // Mute Triangle
      else if (input_value == "TRO") return 81; // Open Triangle
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

	if (accidental == "#" || accidental == "♯" || accidental == "𝄱" || accidental == "𝄰") value++;
	else if (accidental == "b" || accidental == "♭" || accidental == "𝄭" || accidental == "𝄬") value--;
	else {
	  assert(accidental == "-" || accidental == "♮" || accidental == "𝄮" || accidental == "𝄯");
	}

	return value;
      } else if (tuning == Tuning::TET19) {
	auto value = (octave + 1) * 19;

	// C C♯ D♭ D D♯ E♭ E E♯ F♭ F F♯ G♭ G G♯ A♭ A A♯ B♭ B B♯ C♭
	assert(0);
	return value;
      } else if (tuning == Tuning::TET31) {
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

	if (accidental == "#" || accidental == "♯" || accidental == "𝄱" || accidental == "𝄰") value += 2;
	else if (accidental == "b" || accidental == "♭" || accidental == "𝄭" || accidental == "𝄬") value -= 2;
	else if (accidental == "x" || accidental == "𝄪") value += 4;
	else if (accidental == "bb" || accidental == "𝄫") value -= 4;	
	else {
	  assert(accidental.empty() || accidental == "-" || accidental == "♮" || accidental == "𝄮" || accidental == "𝄯");
	}

	return value;
      } else {
	auto value = (octave + 1) * 53;
      
	if (letter == 'C' || letter == 'D') value += (letter - 'C') * 9;
	else if (letter == 'E' || letter == 'F') value += 17 + (letter - 'E') * 5;
	else if (letter == 'G') value += 31;
	else if (letter == 'A' || letter == 'B') value += 39 + (letter - 'A') * 9;
	else {
	  assert(0);
	}

	if (accidental == "x" || accidental == "𝄪") value += 7;
	else if (accidental == "𝄰") value += 4;
	else if (accidental == "#" || accidental == "♯") value += 3;
	else if (accidental == "𝄱") value += 2;
	else if (accidental == "𝄮") value += 1;
	else if (accidental == "𝄯") value -= 1;
	else if (accidental == "𝄬") value -= 2;
	else if (accidental == "b" || accidental == "♭") value -= 3;
	else if (accidental == "𝄭") value -= 4;
	else if (accidental == "bb" || accidental == "𝄫") value -= 7;
	else {
	  assert(accidental.empty() || accidental == "-" || accidental == "♮");
	}

	return value;
      }
    }
  }

  static std::vector<Note> createFromString(std::string_view line, short velocity, short delay, Tuning tuning) {
    std::vector<Note> r;

    std::string_view::size_type pos0 = 0;
    while (pos0 < line.size()) {
      auto pos1 = line.find_first_of(" \t", pos0);
      if (pos1 == std::string_view::npos) pos1 = line.size();

      if (pos0 < pos1) {
	auto s = line.substr(pos0, pos1 - pos0);
	if (s == "off" || s == "OFF") {
	  r.emplace_back(0, 0);
	} else {
	  r.emplace_back(s, velocity, delay, tuning);
	}
      }

      pos0 = pos1 + 1;      
    }

    return r;
  }

 private:
  int value; // sample position, note value or -1 for undefined note
  short velocity;
  short delay;  
};

#endif
