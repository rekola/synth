#ifndef _TUNER_H_
#define _TUNER_H_

#include "Tuning.h"
#include "../model/Note.h"

#include <cmath>

class Tuner {
 public:
  static float getFrequency(Tuning tuning, const Note & note) {
    if (note.isOff() || !note.isDefined() || note.isAftertouch()) {
      return 0.0f;
    } else {
      return getFrequency(tuning, note.getValue());
    }
  }

  static float getFrequency(Tuning tuning, int note_value) {
    switch (tuning) {
    case Tuning::TET12:
    case Tuning::PERCUSSION: return 440.0f * powf(2.0f, (note_value - 69) / 12.0f);
    case Tuning::TET19: return 440.0f * powf(2.0f, (note_value - 109) / 19.0f);
    case Tuning::TET31: return 440.0f * powf(2.0f, (note_value - 178) / 31.0f);
    case Tuning::TET53: return 440.0f * powf(2.0f, (note_value - 304) / 53.0f);
    default:
      return 0.0f;
    }
  }
};

#endif
