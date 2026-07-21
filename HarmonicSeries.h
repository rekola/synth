#ifndef _HARMONICSERIES_H_
#define _HARMONICSERIES_H_

#include "Track.h"
#include "SphericalPosition.h"

class HarmonicSeries : public Track {
 public:
  HarmonicSeries() : Track(TrackType::EFFECT) { }

  const char * getElementName() const override { return "harmonicSeries"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value, float send_a, float send_b) const override;

private:
  int voices_ = 256, from_ = 1, skip_ = 0;
  
  bool undertone_ = true;
  float detune_ = 0;
  float spread_ = 0;
};

#endif
