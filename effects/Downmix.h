#ifndef _DOWNMIX_H_
#define _DOWNMIX_H_

#include "../Track.h"

class Downmix : public Track {
 public:
  Downmix() : Track(TrackType::EFFECT) { }
  
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "downmix"; }
};

#endif
