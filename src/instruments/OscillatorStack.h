#ifndef _OSCILLATORSTACK_H_
#define _OSCILLATORSTACK_H_

// How an oscillator's single voice is stacked: member k of `voices` plays at
// ratio^k times the note's frequency and falloff^k times its level. detune
// (cents) and spread (a multiplier on the position's extent) are spread
// evenly and centred across the members, so ratio 1 makes a detuned unison
// choir and ratio 2 a stack of octaves. The default is one plain member.
struct OscillatorStack {
  static constexpr int kMaxVoices = 64;

  int voices = 1;
  float ratio = 1.0f;
  float falloff = 1.0f;
  float detune_cents = 0.0f;
  float spread = 0.0f;
};

#endif
