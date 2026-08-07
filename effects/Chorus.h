#ifndef _CHORUS_H_
#define _CHORUS_H_

#include "Effect.h"
#include "../AmbisonicEncoding.h"

class Chorus : public Effect {
 public:
  Chorus() { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "chorus"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  // Real stereo-width chorus processing needs genuine 2-channel input, not
  // raw ambisonic channels - same reasoning as Distortion (see
  // AmbisonicEncoding.h).
  ChannelConfiguration getChildChannelConfiguration(const ChannelConfiguration & config) const override { return reduceForEffect(config); }

 private:
  int voices_ = 3;
  float rate_ = 0.5f;   // Hz
  float delay_ = 15.0f; // ms, center delay
  float depth_ = 4.0f;  // ms, modulation depth
  float mix_ = 0.5f;
};

#endif
