

/* LICENSE (MIT)

   Copyright (C) 2021, Mikael Rekola
   Based on TinySoundFont, Copyright (C) 2017, 2018 Bernhard Schelling
   Based on SFZero, Copyright (C) 2012 Steve Folta (https://github.com/stevefolta/SFZero)

   Permission is hereby granted, free of charge, to any person obtaining a copy of this
   software and associated documentation files (the "Software"), to deal in the Software
   without restriction, including without limitation the rights to use, copy, modify, merge,
   publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
   to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
   PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
   LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef _ENVELOPESTATE_H_
#define _ENVELOPESTATE_H_

#include "State.h"
#include "../instruments/Envelope.h"

#include <cmath>

static inline float tsf_timecents2Secsf(float timecents) { return powf(2.0f, timecents / 1200.0); }

// Grace release time for quick voice off (avoid clicking noise)
#define TSF_FASTRELEASETIME 0.01f

class EnvelopeState : public State {
 public:
  enum Segment { NONE = 0, DELAY, ATTACK, HOLD, DECAY, SUSTAIN, RELEASE, DONE };
  
  EnvelopeState()
    : State(0), level(0.0f), slope(0.0f), samplesUntilNextSegment(0), midiVelocity(0), segmentIsExponential(false), isAmpEnv(false) { }

  EnvelopeState(int _outSampleRate, const Envelope & _parameters, int midiNoteNumber, short _midiVelocity, bool _isAmpEnv)
    : State(_outSampleRate),
      parameters(_parameters),
      midiVelocity(_midiVelocity),
      isAmpEnv(_isAmpEnv) {

    if (parameters.keynumToHold_) {
      parameters.hold_ *= tsf_timecents2Secsf(parameters.keynumToHold_ * (60.0f - midiNoteNumber));
    }
    if (parameters.keynumToDecay_) {
      parameters.decay_ *= tsf_timecents2Secsf(parameters.keynumToDecay_ * (60.0f - midiNoteNumber));
    }
	  
    nextSegment(NONE);
  }
  
  void nextSegment(short active_segment) {
    switch (active_segment) {
    case NONE:
      samplesUntilNextSegment = (int)((parameters.delay_) * getOutSampleRate());
      if (samplesUntilNextSegment > 0) {
	segment = DELAY;
	segmentIsExponential = false;
	level = 0.0;
	slope = 0.0;
	return;
      }
      /* fall through */
    case DELAY:
      samplesUntilNextSegment = (int)(parameters.attack_ * getOutSampleRate());
      if (samplesUntilNextSegment > 0) {
	if (!isAmpEnv) {
	  // mod env attack duration scales with velocity (velocity of 1 is full duration, max velocity is 0.125 times duration)
	  samplesUntilNextSegment = (int)(parameters.attack_ * ((145 - midiVelocity) / 144.0f) * getOutSampleRate());
	}
	segment = ATTACK;
	segmentIsExponential = false;
	level = 0.0f;
	slope = 1.0f / samplesUntilNextSegment;
	return;
      }
      /* fall through */
    case ATTACK:
      samplesUntilNextSegment = (int)(parameters.hold_ * getOutSampleRate());
      if (samplesUntilNextSegment > 0) {
	segment = HOLD;
	segmentIsExponential = false;
	level = 1.0f;
	slope = 0.0f;
	return;
      }
      /* fall through */
    case HOLD:
      samplesUntilNextSegment = (int)(parameters.decay_ * getOutSampleRate());
      if (samplesUntilNextSegment > 0) {
	segment = DECAY;
	level = 1.0f;
	if (isAmpEnv) {
	  // I don't truly understand this; just following what LinuxSampler does.
	  float mysterySlope = -9.226f / samplesUntilNextSegment;
	  slope = expf(mysterySlope);
	  segmentIsExponential = true;
	  if (parameters.sustain_ > 0.0f) {
	    // Again, this is following LinuxSampler's example, which is similar to
	    // SF2-style decay, where "decay" specifies the time it would take to
	    // get to zero, not to the sustain level.  The SFZ spec is not that
	    // specific about what "decay" means, so perhaps it's really supposed
	    // to specify the time to reach the sustain level.
	    samplesUntilNextSegment = (int)(log(parameters.sustain_) / mysterySlope);
	  }
	} else {
	  slope = -1.0f / samplesUntilNextSegment;
	  samplesUntilNextSegment = (int)(parameters.decay_ * (1.0f - parameters.sustain_) * getOutSampleRate());
	  segmentIsExponential = false;
	}
	return;
      }
      /* fall through */
    case DECAY:
      segment = SUSTAIN;
      level = parameters.sustain_;
      slope = 0.0f;
      samplesUntilNextSegment = 0x7FFFFFFF;
      segmentIsExponential = false;
      return;
    case SUSTAIN:
      segment = RELEASE;
      samplesUntilNextSegment = (int)((parameters.release_ <= 0 ? TSF_FASTRELEASETIME : parameters.release_) * getOutSampleRate());
      if (isAmpEnv) {
	// I don't truly understand this; just following what LinuxSampler does.
	float mysterySlope = -9.226f / samplesUntilNextSegment;
	slope = expf(mysterySlope);
	segmentIsExponential = true;
      } else {
	slope = -level / samplesUntilNextSegment;
	segmentIsExponential = false;
      }
      return;
    case RELEASE:
    default:
      segment = DONE;
      segmentIsExponential = false;
      level = slope = 0.0f;
      samplesUntilNextSegment = 0x7FFFFFF;
    }
  }

  void process(int numSamples) {
    if (slope) {
      if (segmentIsExponential) level *= powf(slope, (float)numSamples);
      else level += (slope * numSamples);
    }
    if ((samplesUntilNextSegment -= numSamples) <= 0) {
      nextSegment(segment);
    }
  }

  bool isDone() const { return segment == DONE; }
  // Used by the silence-kill threshold (SoundFontVoice::render(),
  // EnvelopeFilterState::applyEffect()) to gate an early-DONE kill to
  // only the release stage - a held note's gain can legitimately be very
  // low during ATTACK (ramping up from 0) or a deliberately quiet
  // SUSTAIN, and must never be killed while still held regardless of how
  // quiet it currently is.
  bool isReleasing() const { return segment == RELEASE; }
  float getLevel() const { return level; }

  Envelope parameters;

private:
  Segment segment = NONE;
  float level, slope;
  int samplesUntilNextSegment;
  short midiVelocity;
  bool segmentIsExponential, isAmpEnv;
};

#endif
