#ifndef _TUNER_H_
#define _TUNER_H_

#include "Tuner.h"

class Tuner {
 public:
  Tuner() { }

  float getFrequency(Tuning tuning, int key, const Note & note) const {
    if (note.isOff() || !note.isDefined()) {
      return 0.0f;
    } else if (key >= 0) { // apply just tuning if key is defined
      int octave_steps = get_octave_steps(tuning);
      key = key % octave_steps;
      int value = note.getValue() - key;
      int octave = value / octave_steps, steps = value % octave_steps;

      auto [ numerator, denominator ] = get_just_interval(tuning, steps);

      if (tuning == Tuning::TET12) {
	return 440.0f * powf(2.0f, (octave * 12 + key - 69.0f) / 12.0f) * numerator / denominator;
      } else if (tuning == Tuning::TET19) {
	return 440.0f * powf(2.0f, (octave * 19 + key - 109.0f) / 19.0f) * numerator / denominator;
      } else if (tuning == Tuning::TET31) {
	return 440.0f * powf(2.0f, (octave * 31 + key - 178.0f) / 31.0f) * numerator / denominator;
      }
    } else if (tuning == Tuning::TET12) {
      return 440.0f * powf(2.0f, (note.getValue() - 69.0f) / 12.0f);
    } else if (tuning == Tuning::TET19) {
      return 440.0f * powf(2.0f, (note.getValue() - 109.0f) / 19.0f);
    } else if (tuning == Tuning::TET31) {
      return 440.0f * powf(2.0f, (note.getValue() - 178.0f) / 31.0f);
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
      case 0: return std::pair(1, 1);
      case 1: return std::pair(25, 24); // or 21:20, 28:27	  
      case 2: return std::pair(15, 14); // or 13:12, 14:13
      case 3: return std::pair(9, 8); // or 10:9
      case 4: return std::pair(15, 13);
      case 5: return std::pair(6, 5);
      case 6: return std::pair(5, 4);
      case 7: return std::pair(9, 7);
      case 8: return std::pair(4, 3);
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
      }
      break;
    case Tuning::TET31:
      switch (steps) {
      case 0: return std::pair(1, 1);
      case 1: return std::pair(45, 44); // or 45/44, 49/48, 46/45, 128/125, 36/35
      case 2: return std::pair(25, 24); // or 25/24, 21/20, 22/21, 23/22
      case 3: return std::pair(16, 15); // or 15/14
      case 4: return std::pair(12, 11); // or 11/10, 35/32
      case 5: return std::pair(9, 8); // or 10/9, 19/17, 28/25
      case 6: return std::pair(8, 7); // or 144/125
      case 7: return std::pair(7, 6); // or 75/64
      case 8: return std::pair(6, 5);
      case 9: return std::pair(11, 9); // or 27/22, 16/13, 60/49, 49/40
      case 10: return std::pair(5, 4);
      case 11: return std::pair(9, 7); // or 14/11, 23/18, 32/25
      case 12: return std::pair(21, 16); // or 13/10, 17/13, 125/96
      case 13: return std::pair(4, 3);
      case 14: return std::pair(11, 8); // 11/8, 15/11, 26/19
      case 15: return std::pair(7, 5);
      case 16: return std::pair(10, 7);
      case 17: return std::pair(16, 11);
      case 18: return std::pair(3, 2);
      case 19: return std::pair(32, 21);
      case 20: return std::pair(14, 9); // or 11/7, 25/16
      case 21: return std::pair(8, 5);
      case 22: return std::pair(18, 11);
      case 23: return std::pair(5, 3);
      case 24: return std::pair(12, 7);
      case 25: return std::pair(7, 4);
      case 26: return std::pair(16, 9); // or 9/5, 34/19, 25/14
      case 27: return std::pair(11, 6); // or 20/11, 64/35
      case 28: return std::pair(28, 15); // or 15/8
      case 29: return std::pair(48, 25); // or 40/21, 21/11, 44/23
      case 30: return std::pair(88, 45); // or 96/49, 45/23, 125/64, 35/18	
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
