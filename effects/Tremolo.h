#ifndef _TREMOLO_H_
#define _TREMOLO_H_

#include "../Track.h"

class Tremolo : public Track {
 public:
  Tremolo() : Track(TrackType::EFFECT) { }
  
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "tremolo"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  float frequency_ = 1.0f;
  float amplitude_ = 0.0f;
  bool use_aftertouch_ = false;
};

#endif
