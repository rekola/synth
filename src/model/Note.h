#ifndef _NOTE_H_
#define _NOTE_H_

#include "Tuning.h"
#include "../util/Velocity.h"
#include "../util/constants.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <cassert>
#include <charconv>
#include <vector>

// One cell of a pattern. Its type is held in its value: a note-on has the
// note's value, and the five largest values mean a lacuna (a span of missing
// notes, shown ~~~), a lost note (a note began on this row but its pitch is gone,
// shown ???), aftertouch (pressure on whatever sounds in the column), an OFF,
// and undefined (an empty cell, or one with only fx). A lost note and a lacuna
// make no sound but end the previous note in their column, as an OFF does. Never
// the velocity: velocity 0 is a real note-on, one that starts from silence.
// There is one 7-bit velocity, always present on a note-on, an OFF and
// aftertouch: the strike velocity, the release velocity (the "lift") and the
// pressure. A lost note and a lacuna have neither velocity nor delay: they are
// not played, and setVelocity()/setDelay() leave them alone.
class Note {
 public:
  Note() = default;
  // A note-on; aftertouch for a negative value (no value). The velocity is 7-bit.
  explicit Note(int _value, int _velocity = constants::NEUTRAL_VELOCITY, int _delay = 0)
      : packed_(pack(_value < 0 ? kAftertouch : clampValue(_value), _velocity)), delay_(clampDelay(_delay)) {}
  // A note-on, or an OFF for the text "off"/"OFF" (the velocity is then its lift).
  explicit Note(std::string_view input_value, Tuning tuning, int _velocity = constants::NEUTRAL_VELOCITY, int _delay = 0, int default_octave = 4)
      : packed_(pack(input_value == "off" || input_value == "OFF" ? kOff : input_value == "???" ? kLost
                                                                       : input_value == "~~~"   ? kLacuna
                                                                                                : clampValue(stringToKey(tuning, input_value, default_octave)),
                     _velocity)),
        delay_(clampDelay(_delay)) {
    if (isLost() || isLacuna()) *this = isLost() ? lost() : lacuna(); // no velocity or delay
  }

  // An OFF with release velocity `lift` (7-bit).
  static Note off(int lift = constants::NEUTRAL_VELOCITY, int delay = 0) {
    Note n;
    n.packed_ = pack(kOff, lift);
    n.delay_ = clampDelay(delay);
    return n;
  }
  // A note that began on this row whose pitch is unknown; it makes no sound but
  // ends the previous note in its column.
  static Note lost() {
    Note n;
    n.packed_ = pack(kLost, constants::NEUTRAL_VELOCITY);
    return n;
  }
  // A span of missing notes, from this row until the column's next cell; it makes
  // no sound but ends the previous note in its column.
  static Note lacuna() {
    Note n;
    n.packed_ = pack(kLacuna, constants::NEUTRAL_VELOCITY);
    return n;
  }
  // Pressure on whatever sounds in the column: a velocity and no value.
  static Note aftertouch(int pressure, int delay = 0) { return Note(-1, pressure, delay); }

  // The value as one int, for storing: a note's value, or -1 undefined, -2
  // aftertouch, -3 OFF, -4 lost, -5 lacuna. fromCode() is its inverse.
  int toCode() const {
    if (isNoteOn()) return getValue();
    return isAftertouch() ? -2 : isOff()  ? -3
                             : isLost()   ? -4
                             : isLacuna() ? -5
                                          : -1;
  }
  static Note fromCode(int code, int velocity, int delay) {
    Note n;
    if (code >= 0)
      n = Note(code, velocity, delay);
    else if (code == -2)
      n = aftertouch(velocity, delay);
    else if (code == -3)
      n = off(velocity, delay);
    else if (code == -4)
      n = lost();
    else if (code == -5)
      n = lacuna();
    if (!n.isDefined()) n.setVelocity(velocity);
    return n;
  }

  // int, not short: value is stored as int (transpose() can legitimately
  // grow it well past SHRT_MAX for a high-EDO tuning many octaves up -
  // see its own comment), and every caller already treats the result as
  // int (getFrequencyFor(Tuning, int)/noteOn's int note_value/...) -
  // returning short here was pure incidental narrowing on the way out,
  // not something any caller actually needed.
  // -1 unless the cell is a note-on.
  int getValue() const { return isNoteOn() ? static_cast<int>(packed_ & kValueMask) : -1; }
  short getVelocity() const { return static_cast<short>((packed_ >> kVelocityShift) & velocity::kMax7); }
  // 0-1. A note-on at velocity 0 is exactly 0: the voice starts silent.
  float getVelocityAsFloat() const { return velocity::toFloat(getVelocity()); }
  short getDelay() const { return delay_; }

  // The note's local effect: three characters, a mnemonic and a two-digit hex
  // argument (`Command` without its leading chain digit), "..." when unset.
  // Stored and edited like velocity/delay. A leading '+' or '-' is not a
  // mnemonic but the sign of a tuning correction in cents (see below).
  std::string_view getFx() const { return std::string_view(fx, 3); }
  bool hasFx() const { return fx[0] != kFxEmpty || fx[1] != kFxEmpty || fx[2] != kFxEmpty; }
  void clearFx() { std::fill(fx, fx + 3, kFxEmpty); }
  // The placeholder for an unset fx character; shown as a middle dot.
  static constexpr char kFxEmpty = '.';
  // Whether `codepoint` may be typed at position `i` (0 = the mnemonic or
  // sign, 1-2 the hex argument); a letter is stored uppercase. Returns the
  // character to store, or 0 if it is not allowed.
  static char fxCharFor(int i, int32_t codepoint) {
    if (i < 0 || i >= 3) return 0;
    if (codepoint >= 'a' && codepoint <= 'z') codepoint -= 'a' - 'A';
    if (codepoint == kFxEmpty || (codepoint >= '0' && codepoint <= '9')) return static_cast<char>(codepoint);
    if (i == 0 && (codepoint == '+' || codepoint == '-')) return static_cast<char>(codepoint);
    if (codepoint >= 'A' && codepoint <= (i == 0 ? 'Z' : 'F')) return static_cast<char>(codepoint);
    return 0;
  }
  // false (and unchanged) unless `text` is exactly three allowed characters.
  bool setFx(std::string_view text) {
    if (text.size() != 3) return false;
    char parsed[3];
    for (int i = 0; i < 3; i++) {
      parsed[i] = fxCharFor(i, static_cast<unsigned char>(text[static_cast<size_t>(i)]));
      if (!parsed[i]) return false;
    }
    std::copy(parsed, parsed + 3, fx);
    return true;
  }
  bool setFxChar(int i, int32_t codepoint) {
    auto c = fxCharFor(i, codepoint);
    if (!c) return false;
    fx[i] = c;
    return true;
  }

  // A tuning correction is the fx `+hh` or `-hh`: a sign and two hex digits,
  // whole cents added to the note's pitch. Whether a note carries one is the
  // mark of it being tuned, so a tuned note with nothing to correct holds +00.
  static constexpr int kMaxTuningCorrectionCents = 255;
  bool hasTuningCorrection() const {
    return (fx[0] == '+' || fx[0] == '-') && hexDigit(fx[1]) >= 0 && hexDigit(fx[2]) >= 0;
  }
  // 0 when the note carries none.
  int getTuningCorrectionCents() const {
    if (!hasTuningCorrection()) return 0;
    int magnitude = hexDigit(fx[1]) * 16 + hexDigit(fx[2]);
    return fx[0] == '-' ? -magnitude : magnitude;
  }
  // Replaces the whole fx (clamped to +-kMaxTuningCorrectionCents).
  void setTuningCorrection(int cents) {
    int magnitude = std::min(std::abs(cents), kMaxTuningCorrectionCents);
    static const char kHex[] = "0123456789ABCDEF";
    fx[0] = cents < 0 ? '-' : '+';
    fx[1] = kHex[magnitude >> 4];
    fx[2] = kHex[magnitude & 15];
  }
  float getDelayAsFloat() const { return delay_ / 255.0f; }
  // A note-on, an OFF or aftertouch (not an empty cell or one with only fx).
  bool isDefined() const { return value() != kUndefined; }
  // A cell worth keeping: a note, or only fx (a command for the column, such
  // as 0Gxx's glide rate or a retrigger, with no note of its own). Storage and
  // display go by this; playback and editing of notes by isDefined().
  bool hasContent() const { return isDefined() || hasFx(); }
  bool isNoteOn() const { return value() < kLacuna; }
  bool isLost() const { return value() == kLost; }
  bool isLacuna() const { return value() == kLacuna; }
  bool isOff() const { return value() == kOff; }
  bool isAftertouch() const { return value() == kAftertouch; }

  // A note-on, an OFF or aftertouch has a velocity and a delay; a lost note and a
  // lacuna have neither (and an empty cell has none to show).
  bool hasVelocityAndDelay() const { return isNoteOn() || isOff() || isAftertouch(); }

  // Held to 0-127. Not for a lost note or a lacuna.
  void setVelocity(int v) {
    if (isLost() || isLacuna()) return;
    packed_ = (packed_ & ~(static_cast<uint32_t>(velocity::kMax7) << kVelocityShift)) | (clampVelocity(v) << kVelocityShift);
  }
  void setDelay(int d) {
    if (!isLost() && !isLacuna()) delay_ = clampDelay(d);
  }

  void clear() {
    packed_ = pack(kUndefined, constants::NEUTRAL_VELOCITY);
    delay_ = 0;
    clearFx();
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
    if (!isNoteOn()) return;
    auto next = static_cast<int64_t>(getValue()) + delta;
    packed_ = pack(clampValue(static_cast<int>(std::clamp<int64_t>(next, 0, kMaxValue))), getVelocity());
  }

  std::string toString(Tuning tuning) const {
    if (isDefined() && !isAftertouch()) {
      if (isLacuna()) {
        return "~~~";
      } else if (isLost()) {
        return "???";
      } else if (isOff()) {
        return "OFF";
      } else {
        auto key_name = keyToString(tuning, getValue());
	if (tuning != Tuning::PERCUSSION) {
	  if (key_name.size() == 1) key_name += '-';
          int period = tuning == Tuning::EDO53 ? 53 : tuning == Tuning::EDO31 ? 31
                                                  : tuning == Tuning::EDO24   ? 24
                                                  : tuning == Tuning::EDO19   ? 19
                                                                              : 12;
          int octave = (getValue() / period) - 1;
	  // A C-spelled name at the top of the index range (C♭) belongs to
	  // the next octave number, though its value is still below it.
	  if (key_name[0] == 'C' && getValue() % period > period / 2) octave++;
	  key_name += std::to_string(octave);
	}
	return key_name;
      }
    } else {
      return "···";
    }
  }

  static inline std::string keyToString(Tuning tuning, int value) {
    static const char * note_names_53edo[] = {
        "C", "C↑", "D𝄫", "C♯", "C𝄰", "D𝄭", "D♭", "C𝄪", "D↓", "D", "E𝄫", "D𝄱", "D♯", "D𝄰",
        "E♭", "F𝄫", "D𝄪", "E", "E↑", "F♭", "E♯", "F↓", "F", "F↑", "E𝄪", "F♯", "F𝄰",
        "G𝄭", "G♭", "F𝄪", "G↓", "G", "A𝄫", "G𝄱", "G♯", "A𝄭", "A♭", "A𝄬", "G𝄪", "A",
        "A↑", "B𝄫", "A♯", "A𝄰", "B𝄭", "B♭", "A𝄪", "B↓", "B", "B↑", "B𝄱", "B♯", "B𝄰"};
    static const char * note_names_31edo[] = { "C", "D𝄫", "C♯", "D♭", "C𝄪", "D", "E𝄫", "D♯",
					       "E♭", "D𝄪", "E", "F♭", "E♯", "F", "G𝄫", "F♯",
					       "G♭", "F𝄪", "G", "A𝄫", "G♯", "A♭", "G𝄪", "A",
					       "B𝄫", "A♯", "B♭", "A𝄪", "B", "C♭", "B♯" };
    static const char * note_names_24edo[] = {"C", "C𝄲", "C♯", "D𝄳", "D", "D𝄲", "D♯", "E𝄳",
                                              "E", "E𝄲", "F", "F𝄲", "F♯", "G𝄳", "G", "G𝄲",
                                              "G♯", "A𝄳", "A", "A𝄲", "A♯", "B𝄳", "B", "B𝄲"};
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
    } else if (tuning == Tuning::EDO53) {
      return note_names_53edo[value % 53];
    } else if (tuning == Tuning::EDO31) {
      return note_names_31edo[value % 31];
    } else if (tuning == Tuning::EDO24) {
      return note_names_24edo[value % 24];
    } else if (tuning == Tuning::EDO19) {
      return note_names_19edo[value % 19];
    } else {
      return note_names_12edo[value % 12];
    }
  }

  static inline int stringToKey(Tuning tuning, std::string_view input_value, int default_octave = 4) {
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
      auto octave = default_octave;
      while (!input_value.empty() && input_value[input_value.size() - 1] == '\'') {
        input_value.remove_suffix(1);
        octave++;
      }
      auto pos = input_value.find_first_of("0123456789");
      if (pos != std::string_view::npos) {
	auto octave_text = input_value.substr(pos);
	auto result = std::from_chars(octave_text.data(), octave_text.data() + octave_text.size(), octave);
	if (result.ec == std::errc::invalid_argument) return 0;
	input_value.remove_suffix(octave_text.size());
      }
      auto letter = input_value[0];
      auto accidental = input_value.substr(1);

      // An arrow after the letter or its accidental (B↑, A♯↑, D♭↓) is one
      // 53-EDO step up or down; every other tuning ignores it.
      int arrow = 0;
      if (accidental.ends_with("↑")) {
        arrow = 1;
        accidental.remove_suffix(std::string_view("↑").size());
      } else if (accidental.ends_with("↓")) {
        arrow = -1;
        accidental.remove_suffix(std::string_view("↓").size());
      }

      assert(letter >= 'A' && letter <= 'G');
      
      if (tuning == Tuning::EDO12) {
	auto value = (octave + 1) * 12;
      
	// C C# D D# E F F# G G# A A# B

	if (letter >= 'C' && letter <= 'E') value += (letter - 'C') * 2;
	else if (letter == 'F' || letter == 'G') value += 5 + (letter - 'F') * 2;
	else if (letter == 'A' || letter == 'B') value += 9 + (letter - 'A') * 2;
	else {
	  assert(0);
	}

	if (accidental == "x" || accidental == "𝄪") value += 2;
	else if (accidental == "#" || accidental == "♯" || accidental == "𝄱" || accidental == "𝄰") value++;
	else if (accidental == "bb" || accidental == "𝄫") value -= 2;
	else if (accidental == "b" || accidental == "♭" || accidental == "𝄭" || accidental == "𝄬") value--;
	else {
          assert(accidental.empty() || accidental == "-" || accidental == "♮");
        }

        return value;
      } else if (tuning == Tuning::EDO19) {
	auto value = (octave + 1) * 19;

	// C C♯ D♭ D D♯ E♭ E E♯/F♭ F F♯ G♭ G G♯ A♭ A A♯ B♭ B B♯/C♭ -
	// docs/19edo_note_numbers.txt is this table's own source of truth.
	// W-W-H-W-W-W-H with W=3 steps, H=2 - unlike EDO12/31/53's uniform
	// "+N per natural-letter run" shape, 19edo's own H falls in the
	// middle of the {C,D,E}/{F,G,A} groups (E-F, not just B-C), so each
	// letter's own base offset is spelled out directly instead.
	if (letter == 'C') value += 0;
	else if (letter == 'D') value += 3;
	else if (letter == 'E') value += 6;
	else if (letter == 'F') value += 8;
	else if (letter == 'G') value += 11;
	else if (letter == 'A') value += 14;
	else if (letter == 'B') value += 17;
	else {
	  assert(0);
	}

	if (accidental == "x" || accidental == "𝄪") value += 2;
        else if (accidental == "#" || accidental == "♯" || accidental == "𝄱" || accidental == "𝄰")
          value += 1;
	else if (accidental == "bb" || accidental == "𝄫") value -= 2;
        else if (accidental == "b" || accidental == "♭" || accidental == "𝄭" || accidental == "𝄬")
          value -= 1;
	else {
          assert(accidental.empty() || accidental == "-" || accidental == "♮");
        }

        return value;
      } else if (tuning == Tuning::EDO24) {
        auto value = (octave + 1) * 24;

        if (letter == 'C')
          value += 0;
        else if (letter == 'D')
          value += 3;
        else if (letter == 'E')
          value += 7;
        else if (letter == 'F')
          value += 10;
        else if (letter == 'G')
          value += 13;
        else if (letter == 'A')
          value += 17;
        else if (letter == 'B')
          value += 21;
        else {
          assert(0);
        }

        if (accidental == "𝄲" || accidental == "+")
          value += 1;
        else if (accidental == "#" || accidental == "♯" || accidental == "𝄱" || accidental == "𝄰")
          value += 2;
        else if (accidental == "𝄳" || accidental == "d")
          value -= 1;
        else if (accidental == "b" || accidental == "♭" || accidental == "𝄭" || accidental == "𝄬")
          value -= 2;
        else {
          assert(accidental.empty() || accidental == "-" || accidental == "♮" || accidental == "↑" || accidental == "↓");
        }

        return value;
      } else if (tuning == Tuning::EDO31) {
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
          assert(accidental.empty() || accidental == "-" || accidental == "♮");
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

        // 𝄰 𝄱 𝄬 𝄭 are older one-glyph spellings of ♯↑ ♯↓ ♭↑ ♭↓, still read.
        // A double sharp is 35:32, 7 steps, not two sharps: 31-EDO's double
        // sharp is 35:32 too, so a septimal spelling (E𝄫 8:7) means the same in both.
        if (accidental == "x" || accidental == "𝄪") value += 7;
	else if (accidental == "𝄰") value += 4;
	else if (accidental == "#" || accidental == "♯") value += 3;
	else if (accidental == "𝄱") value += 2;
        else if (accidental == "𝄬")
          value -= 2;
        else if (accidental == "b" || accidental == "♭")
          value -= 3;
        else if (accidental == "𝄭") value -= 4;
	else if (accidental == "bb" || accidental == "𝄫") value -= 7;
	else {
	  assert(accidental.empty() || accidental == "-" || accidental == "♮");
	}
        value += arrow;

        return value;
      }
    }
  }

  static std::vector<Note> createFromString(std::string_view line, int velocity, int delay, Tuning tuning) {
    std::vector<Note> r;

    std::string_view::size_type pos0 = 0;
    while (pos0 < line.size()) {
      auto pos1 = line.find_first_of(" \t", pos0);
      if (pos1 == std::string_view::npos) pos1 = line.size();

      if (pos0 < pos1) {
	auto s = line.substr(pos0, pos1 - pos0);
        if (s == "~~~") {
          r.push_back(lacuna()); // it has no velocity or delay, whatever the file says
        } else if (s == "???") {
          r.push_back(lost());
        } else if (s == "off" || s == "OFF") {
          r.push_back(off(velocity, delay));
        } else {
          r.emplace_back(s, tuning, velocity, delay);
        }
      }

      pos0 = pos1 + 1;      
    }

    return r;
  }

 private:
  // 8 bytes: one word holding the value (25 bits) and the velocity (7), then the
  // delay and the three fx characters. The top five values are the other types.
  static constexpr int kVelocityShift = 25;
  static constexpr uint32_t kValueMask = (1u << kVelocityShift) - 1;
  static constexpr uint32_t kUndefined = kValueMask, kOff = kValueMask - 1, kAftertouch = kValueMask - 2, kLost = kValueMask - 3,
                            kLacuna = kValueMask - 4;
  static constexpr uint32_t kMaxValue = kValueMask - 5;
  static constexpr uint32_t clampValue(int value) { return static_cast<uint32_t>(std::clamp<int64_t>(value, 0, kMaxValue)); }
  static constexpr uint32_t pack(uint32_t value, int velocity) {
    return (static_cast<uint32_t>(std::clamp(velocity, 0, 127)) << kVelocityShift) | value;
  }
  uint32_t value() const { return packed_ & kValueMask; }
  static uint32_t clampVelocity(int v) { return static_cast<uint32_t>(std::clamp(v, 0, velocity::kMax7)); }
  static uint8_t clampDelay(int d) { return static_cast<uint8_t>(std::clamp(d, 0, 255)); }

  uint32_t packed_ = pack(kUndefined, constants::NEUTRAL_VELOCITY);
  uint8_t delay_ = 0;
  static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  char fx[3] = { kFxEmpty, kFxEmpty, kFxEmpty };
};

// Notes are copied around by value, in registers; keep one at 8 bytes.
static_assert(sizeof(Note) == 8, "Note must stay 8 bytes");

// The Note-taking overload of getFrequencyFor() (Tuning.h) lives here
// rather than there, since Tuning.h is included by Note.h and can't
// include it back without the two headers including each other.
inline float getFrequencyFor(Tuning tuning, const Note & note) {
  if (!note.isNoteOn()) {
    return 0.0f;
  }
  return getFrequencyFor(tuning, note.getValue());
}

#endif
