#ifndef _EQUALIZER_H_
#define _EQUALIZER_H_

#include "Effect.h"
#include "../dsp/FilterType.h"

// Parametric equalizer: kBands biquads in series (dsp/Biquad.h), each applied
// to every channel. A peak or shelf band at 0 dB is skipped, as is any band
// that is switched off.
class Equalizer : public Effect {
 public:
  static constexpr int kBands = 8;
  static constexpr float kMinFreq = 20.0f, kMaxFreq = 20000.0f;
  static constexpr float kMinGainDb = -24.0f, kMaxGainDb = 24.0f;
  static constexpr float kMinQ = 0.1f, kMaxQ = 18.0f;

  struct Band {
    FilterType type = FilterType::peak;
    float freq = 1000.0f;
    float gain_db = 0.0f;  // peak and shelf types only
    float q = 1.0f;
    bool on = true;

    // Peak and shelf bands have a gain; the others are fixed-shape filters.
    bool hasGain() const { return type == FilterType::peak || type == FilterType::lowshelf || type == FilterType::highshelf; }
    // Whether the band changes the signal.
    bool isActive() const { return on && (!hasGain() || gain_db != 0.0f); }

    bool operator==(const Band & o) const { return type == o.type && freq == o.freq && gain_db == o.gain_db && q == o.q && on == o.on; }
    bool operator!=(const Band & o) const { return !(*this == o); }
  };

  Equalizer();

  const Band & getBand(int i) const { return bands_[i]; }
  // Clamps the band's values into range.
  void setBand(int i, Band band);

  // The combined magnitude response, dB, at `hz` for a given sample rate.
  float responseDb(float hz, float sample_rate) const;

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & config) const override;
  const char * getElementName() const override { return "equalizer"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  Band bands_[kBands];
};

#endif
