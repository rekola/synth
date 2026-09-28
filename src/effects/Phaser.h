#ifndef _PHASER_H_
#define _PHASER_H_

#include "Effect.h"

// A classic swept-notch phaser: `stages` cascaded first-order allpass
// filters (AllpassStage, dsp/AllpassFilter.h), all sharing one
// instantaneous crossover frequency that an LFO sweeps exponentially
// between minFreq/maxFreq, summed back with the dry signal - the moving
// phase cancellation between the two is what produces the characteristic
// sweeping notches, not any gain change (an allpass stage never changes
// gain, only phase). `feedback` routes some of the cascade's own output
// back into its input for a more resonant, pronounced sweep, the classic
// "more metallic/vocal" phaser character at higher settings.
//
// A linear, per-instant-static-frequency filter effect (unlike Distortion
// or Chorus, which are MonoEffect - a nonlinear waveshaper or a spatial
// decorrelation trick doesn't commute with ambisonic encoding the way a
// linear filter does) - modeled directly on BiquadFilter's own shape:
// every channel (Main and AuxA/AuxB alike) is filtered independently by
// its own persistent allpass-chain state, all driven by one shared LFO
// phase so a static source's own direction is preserved exactly, the same
// reasoning BiquadFilter.h's own class comment gives.
class Phaser : public Effect {
 public:
  Phaser() { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & config) const override;
  const char * getElementName() const override { return "phaser"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  int stages_ = 4;
  float rate_ = 0.5f;      // Hz
  float min_freq_ = 200.0f;  // Hz
  float max_freq_ = 2000.0f; // Hz
  float feedback_ = 0.0f;    // -0.95..0.95
  float mix_ = 0.5f;
};

#endif
