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

  // Track is playing if it's child is active or it is active by itself
  virtual bool isActive() const {
    return TrackState::isActive() || isEffectActive();
  }
  
 protected:
  virtual void applyEffect(SampleData & input) = 0;
  void setEffectActive(bool is_effect_active) { is_effect_active_ = is_effect_active; }
  bool isEffectActive() const { return is_effect_active_; }
  
private:
  bool is_effect_active_ = false;
};

#endif
