#ifndef _CHORUS_H_
#define _CHORUS_H_

#include "Track.h"

class Chorus : public Track {
 public:
  Chorus(float delay1 = 0.0f, float delay2 = 0.0f) : Track(TrackType::EFFECT), delay1_(delay1), delay2_(delay2) { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  std::string getElementName() const override { return "chorus"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  float delay1_, delay2_; // ms
};

#endif
