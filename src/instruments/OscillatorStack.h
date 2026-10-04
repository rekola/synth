#ifndef _OSCILLATORSTACK_H_
#define _OSCILLATORSTACK_H_

// How an oscillator's single voice is stacked: member k of `voices` plays at
// ratio^k times the note's frequency and falloff^k times its level, and its
// detune (cents) is spread evenly and centred across the members, so ratio 1
// makes a detuned unison choir and ratio 2 a stack of octaves. `spread` is a
// multiplier on the position's extent: the radius of the cloud of directions
// the members are dealt into. `drift` (cents, 0 = off) is the peak of a slow
// aperiodic pitch wander each member gets of its own, moving over about
// `drift_period` seconds (see PitchDrift.h), so the stack never settles into a
// repeating beat pattern. The default is one plain member.
struct OscillatorStack {
  static constexpr int kMaxVoices = 256;

  int voices = 1;
  float ratio = 1.0f;
  float falloff = 1.0f;
  float detune_cents = 0.0f;
  float spread = 0.0f;
  float drift_cents = 0.0f;
  float drift_period = 2.0f;
};

#endif
