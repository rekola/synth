#ifndef _DELAY_H_
#define _DELAY_H_

#include "Track.h"

class Delay : public Track {
 public:
  Delay(int _delay = 0.0f, float _fd = 0.0f, float _delaymix = 0.0f) : Track(TrackType::EFFECT), delay(_delay), fd(_fd), delaymix(_delaymix) {
  }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  std::string getElementName() const override { return "delay"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

 private:
  int delay;
  float fd, delaymix;
};

#endif
