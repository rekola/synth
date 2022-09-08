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

#if 0
    setTrackInfo(TrackInfo( is_active, data.isClipping() ));
#endif
    
    return data;    
  }

  // For rendering tracks
  virtual SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) {
    auto data = TrackState::render(frames, instruments, context);
    applyEffect(data);
#if 0    
    setTrackInfo(TrackInfo( is_active, data.isClipping() ));
#endif
    return data;
  }

 protected:
  virtual void applyEffect(SampleData & input) = 0;
};

#endif
