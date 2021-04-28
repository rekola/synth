#ifndef _NOTE_H_
#define _NOTE_H_

#include "Tuning.h"

#include <cmath>
#include <string>

class Note {
 public:
  explicit Note() : note_number(0), velocity(0x3f), numerator(0), denominator(0) { }
  explicit Note(int _numerator, int _denominator, short _velocity) : note_number(0), velocity(_velocity), numerator(_numerator), denominator(_denominator) { }
  explicit Note(int _note_number, short _velocity) : note_number(_note_number), velocity(_velocity), numerator(0), denominator(0) { }

  short getNoteNumber() const { return note_number; }
  short getVelocity() const { return velocity; }
  float getVelocityAsFloat() const { return (float)velocity / 0x3f; }
  bool isDefined() const { return note_number > 0 || (numerator != 0 && denominator != 0); }
  bool isOff() const { return note_number == 1 || velocity == 0; }
  
  inline float getFrequency(Tuning tuning, int transpose, int detune) {
    if (note_number > 1) {
      if (tuning == Tuning::TET31) {
	return 0;
      } else {
	return 440 * powf(2, (note_number - 69.0 + transpose + detune / 100.0f) / 12.0);
      }
    } else {
      return 261.63f * numerator / denominator;
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
      if (numerator == 1 && denominator == 1) {
	return "P1";
      } else if (numerator == 16 && denominator == 15) {
	return "m2";
      } else if ((numerator == 10 && denominator == 9) || (numerator == 9 && denominator == 8)) {
	return "M2";
      } else if (numerator == 8 && denominator == 7) {
	return "S2"; // supermajor second (https://en.wikipedia.org/wiki/Septimal_whole_tone)
      } else if (numerator == 7 && denominator == 6) {
	return "s3"; // subminor third
      } else if (numerator == 6 && denominator == 5) {	
	return "m3";
      } else if (numerator == 5 && denominator == 4) {
	return "M3";
      } else if (numerator == 9 && denominator == 7) {
	return "S3";
      } else if (numerator == 4 && denominator == 3) {
	return "P4";
      } else if (numerator == 3 && denominator == 2) {
	return "P5";
      } else if (numerator == 8 && denominator == 5) {
	return "m6";
      } else if (numerator == 5 && denominator == 3) {
	return "M6";
      } else if ((numerator == 16 && denominator == 9) || (numerator == 9 && denominator == 5)) {
	return "m7";
      } else if (numerator == 15 && denominator == 8) {
	return "M7";
      } else if (numerator == 2 && denominator == 1) {
	return "P8";
      } else {
	return std::to_string(numerator) + ":" + std::to_string(denominator);
      }
    }
    return "???";
  }

 private:
  short note_number, velocity;
  short numerator, denominator; 
};

#endif
