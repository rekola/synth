#ifndef _EQUALIZER_H_
#define _EQUALIZER_H_

#include "Effect.h"

// Four-band parametric equalizer: a low shelf, two peaking bands and a high
// shelf, each a biquad (dsp/Biquad.h) applied to every channel. A band with
// 0 dB gain is skipped.
class Equalizer : public Effect {
 public:
  struct Band {
    float freq, gain_db, q;
  };
  static constexpr int kBands = 4;

  Equalizer();

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & config) const override;
  const char * getElementName() const override { return "equalizer"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  Band bands_[kBands];
};

#endif
