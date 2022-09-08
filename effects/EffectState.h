#ifndef _EFFECTSTATE_H_
#define _EFFECTSTATE_H_

#include "../TrackState.h"

class EffectState : public TrackState {
 public:
  explicit EffectState(const ChannelConfiguration & channel_config) : TrackState(channel_config) { }

  // For rendering voices
  SampleData render(int frames) override {
    auto data = TrackState::render(frames);

    applyEffect(data);
    setTrackInfo(TrackInfo( is_active, data.isClipping() ));
    
    return data;    
  }

  // For rendering tracks
  virtual SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) {
    auto sd = TrackState::render(frames, instruments, context);
    applyEffect(sd);
    setTrackInfo(TrackInfo( active, sd.isClipping() ));
    return sd;
  }

 protected:
  virtual void applyEffect(SampleData & input) = 0;
};

#endif
