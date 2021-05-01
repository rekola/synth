#ifndef _TUNER_H_
#define _TUNER_H_

#include "Tuner.h"

class Tuner {
 public:
  Tuner() { }

  float getFrequency(Tuning tuning, int key, const Note & note, int transpose) const {
    if (note.isOff() || !note.isDefined()) {
      return 0.0f;
    } else if (key >= 0 && tuning != Tuning::TET31) { // apply just tuning if key is defined
      int octave_steps = get_octave_steps(tuning);
      key = key % octave_steps;
      int value = note.getValue() - key;
      int octave = value / octave_steps, steps = value % octave_steps;

      auto [ numerator, denominator ] = get_just_interval(tuning, steps);

      if (tuning == Tuning::TET12) {
	return 440.0f * powf(2.0f, (octave * 12 + key - 69.0f) / 12.0f) * numerator / denominator;
      } else if (tuning == Tuning::TET31) {
	return 440.0f * powf(2.0f, (octave * 31 + key - 178.0f) / 31.0f) * numerator / denominator;
      }
    } else if (tuning == Tuning::TET31) {
      return 440.0f * powf(2.0f, (note.getValue() - 178.0f) / 31.0f);
    } else if (tuning == Tuning::TET12) {
      assert(0);
      return 440.0f * powf(2.0f, (note.getValue() - 69.0f) / 12.0f);
    }
    
    assert(0);
    return 0.0f;
  }

private:
  static inline int get_octave_steps(Tuning tuning) {
    switch (tuning) {
    case Tuning::TET5: return 5;
    case Tuning::TET7: return 7;
    case Tuning::TET12: return 12;
    case Tuning::TET19: return 19;
    case Tuning::TET31: return 31;
    default:
      assert(0);
      return 1;
    }
  }
  
  static inline std::pair<int, int> get_just_interval(Tuning tuning, int steps) {
    switch (tuning) {
    case Tuning::TET12:
      switch (steps) {
      case 0: return std::pair(1, 1);
      case 1: return std::pair(16, 15);
      case 2: return std::pair(9, 8);
      case 3: return std::pair(6, 5);
      case 4: return std::pair(5, 4);
      case 5: return std::pair(4, 3);
      case 6: return std::pair(7, 5);
      case 7: return std::pair(3, 2);
      case 8: return std::pair(8, 5);
      case 9: return std::pair(5, 3);
      case 10: return std::pair(16, 9);
      case 11: return std::pair(15, 8);
      }
      break;
    case Tuning::TET19:
      switch (steps) {
      case 9: return std::pair(7, 5); // or 18:13
      case 10: return std::pair(10, 7); // or 13, 9
      case 11: return std::pair(3, 2);
      case 12: return std::pair(14, 9); // wrong?
      case 13: return std::pair(8, 5); // wrong?
      case 14: return std::pair(5, 3);
      case 15: return std::pair(7, 4);
      case 16: return std::pair(9, 5);
      case 17: return std::pair(15, 8);
      case 18: return std::pair(27, 14);
      case 8: return std::pair(4, 3);
      case 7: return std::pair(9, 7);
      case 6: return std::pair(5, 4);
      case 5: return std::pair(6, 5);
      case 4: return std::pair(15, 13);
      case 3: return std::pair(9, 8); // or 10:9
      case 2: return std::pair(15, 14); // or 13:12, 14:13
      case 1: return std::pair(25, 24); // or 21:20, 28:27	  
      }
      break;
    case Tuning::TET31:
      switch (steps) {
	  
      }
      break;
    default:
      break;
    }
    assert(0);
    return std::pair(0, 0);
  }
};

#endif
