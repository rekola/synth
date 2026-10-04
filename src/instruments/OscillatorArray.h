#ifndef _OSCILLATORARRAY_H_
#define _OSCILLATORARRAY_H_

// How an oscillator's single voice is an array: member k of `voices` plays at
// ratio^k times the note's frequency and falloff^k times its level, and its
// detune (cents) is spread evenly and centred across the members, so ratio 1
// makes a detuned unison choir and ratio 2 an array of octaves. `spread` is a
// multiplier on the position's extent: the radius of the cloud of directions
// the members are dealt into. A detuned array (`detune_cents` > 0, more than one
// member) also lets each member wander slowly and aperiodically in pitch, by
// up to half the detune and over about `drift_period` seconds (0 = off; see
// PitchDrift.h), so it never settles into a repeating beat pattern; no detune,
// no drift. The default is one plain member.
struct OscillatorArray {
  static constexpr int kMaxVoices = 256;

  int voices = 1;
  float ratio = 1.0f;
  float falloff = 1.0f;
  float detune_cents = 0.0f;
  float spread = 0.0f;
  float drift_period = 2.0f;
};

#endif
