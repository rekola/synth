#ifndef _PLAYBACKEVENT_H_
#define _PLAYBACKEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "PlaybackInfo.h"

class PlaybackEvent : public Event {
public:
  PlaybackEvent(const PlaybackInfo & info) : info_(info) { }

  void dispatch(EventHandler & evh) override { evh.handlePlaybackEvent(*this); }
  
  void setLoudness(std::vector<float> loudness) { loudness_ = std::move(loudness); }
  const std::vector<float> & getLoudness() const { return loudness_; }
  
  void setFFT(std::vector<float> data) { fft_data_ = std::move(data); }
  const std::vector<float> & getFFT() const { return fft_data_; }

  void setInfo(PlaybackInfo info) { info_ = std::move(info); }
  const PlaybackInfo & getInfo() const { return info_; }

private:
  std::vector<float> loudness_;
  std::vector<float> fft_data_;
  PlaybackInfo info_;
};

#endif
