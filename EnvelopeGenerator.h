#ifndef _ENVELOPEGENERATOR_H_
#define _ENVELOPEGENERATOR_H_

#include "Envelope.h"

static inline float tsf_timecents2Secsf(float timecents) { return powf(2.0f, timecents / 1200.0f); }

// Grace release time for quick voice off (avoid clicking noise)
#define TSF_FASTRELEASETIME 0.01f

enum { TSF_SEGMENT_NONE, TSF_SEGMENT_DELAY, TSF_SEGMENT_ATTACK, TSF_SEGMENT_HOLD, TSF_SEGMENT_DECAY, TSF_SEGMENT_SUSTAIN, TSF_SEGMENT_RELEASE, TSF_SEGMENT_DONE };

class EnvelopeGenerator {
 public:
  EnvelopeGenerator()
    : level(0.0f), slope(0.0f), samplesUntilNextSegment(0), segment(0), midiVelocity(0), segmentIsExponential(false), isAmpEnv(false), outSampleRate(0) { }

  EnvelopeGenerator(const Envelope & _parameters, int midiNoteNumber, short _midiVelocity, bool _isAmpEnv, float _outSampleRate)
    : midiVelocity(_midiVelocity),
      parameters(_parameters),
      isAmpEnv(_isAmpEnv),
      outSampleRate(_outSampleRate) {
    
    if (parameters.keynumToHold) {
      parameters.hold += parameters.keynumToHold * (60.0f - midiNoteNumber);
      parameters.hold = (parameters.hold < -10000.0f ? 0.0f : tsf_timecents2Secsf(parameters.hold));
    }
    if (parameters.keynumToDecay) {
      parameters.decay += parameters.keynumToDecay * (60.0f - midiNoteNumber);
      parameters.decay = (parameters.decay < -10000.0f ? 0.0f : tsf_timecents2Secsf(parameters.decay));
    }
    
    nextSegment(TSF_SEGMENT_NONE);
  }

  void nextSegment(short active_segment) {
    switch (active_segment) {
    case TSF_SEGMENT_NONE:
      samplesUntilNextSegment = (int)(parameters.delay * outSampleRate);
      if (samplesUntilNextSegment > 0) {
	segment = TSF_SEGMENT_DELAY;
	segmentIsExponential = false;
	level = 0.0;
	slope = 0.0;
	return;
      }
      /* fall through */
    case TSF_SEGMENT_DELAY:
      samplesUntilNextSegment = (int)(parameters.attack * outSampleRate);
      if (samplesUntilNextSegment > 0) {
	if (!isAmpEnv) {
	  // mod env attack duration scales with velocity (velocity of 1 is full duration, max velocity is 0.125 times duration)
	  samplesUntilNextSegment = (int)(parameters.attack * ((145 - midiVelocity) / 144.0f) * outSampleRate);
	}
	segment = TSF_SEGMENT_ATTACK;
	segmentIsExponential = false;
	level = 0.0f;
	slope = 1.0f / samplesUntilNextSegment;
	return;
      }
      /* fall through */
    case TSF_SEGMENT_ATTACK:
      samplesUntilNextSegment = (int)(parameters.hold * outSampleRate);
      if (samplesUntilNextSegment > 0) {
	segment = TSF_SEGMENT_HOLD;
	segmentIsExponential = false;
	level = 1.0f;
	slope = 0.0f;
	return;
      }
      /* fall through */
    case TSF_SEGMENT_HOLD:
      samplesUntilNextSegment = (int)(parameters.decay * outSampleRate);
      if (samplesUntilNextSegment > 0) {
	segment = TSF_SEGMENT_DECAY;
	level = 1.0f;
	if (isAmpEnv) {
	  // I don't truly understand this; just following what LinuxSampler does.
	  float mysterySlope = -9.226f / samplesUntilNextSegment;
	  slope = expf(mysterySlope);
	  segmentIsExponential = true;
	  if (parameters.sustain > 0.0f) {
	    // Again, this is following LinuxSampler's example, which is similar to
	    // SF2-style decay, where "decay" specifies the time it would take to
	    // get to zero, not to the sustain level.  The SFZ spec is not that
	    // specific about what "decay" means, so perhaps it's really supposed
	    // to specify the time to reach the sustain level.
	    samplesUntilNextSegment = (int)(log(parameters.sustain) / mysterySlope);
	  }
	} else {
	  slope = -1.0f / samplesUntilNextSegment;
	  samplesUntilNextSegment = (int)(parameters.decay * (1.0f - parameters.sustain) * outSampleRate);
	  segmentIsExponential = false;
	}
	return;
      }
      /* fall through */
    case TSF_SEGMENT_DECAY:
      segment = TSF_SEGMENT_SUSTAIN;
      level = parameters.sustain;
      slope = 0.0f;
      samplesUntilNextSegment = 0x7FFFFFFF;
      segmentIsExponential = false;
      return;
    case TSF_SEGMENT_SUSTAIN:
      segment = TSF_SEGMENT_RELEASE;
      samplesUntilNextSegment = (int)((parameters.release <= 0 ? TSF_FASTRELEASETIME : parameters.release) * outSampleRate);
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
    case TSF_SEGMENT_RELEASE:
    default:
      segment = TSF_SEGMENT_DONE;
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

  float level, slope;
  int samplesUntilNextSegment;
  short segment, midiVelocity;
  Envelope parameters;
  bool segmentIsExponential, isAmpEnv;
  float outSampleRate;
};

#endif
