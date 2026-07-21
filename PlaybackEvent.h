#ifndef _PLAYBACKEVENT_H_
#define _PLAYBACKEVENT_H_

#include "Event.h"
#include "EventHandler.h"
#include "PlaybackInfo.h"

class PlaybackEvent : public Event {
public:
  PlaybackEvent(const PlaybackInfo & info) : info_(info) { }

  void dispatch(EventHandler & evh) override { evh.handlePlaybackEvent(*this); }
  
  // Raw, pre-mixdown per-channel loudness (ambisonic bus channels, however
  // many are active, then always SendA/SendB last) - feeds the UI's
  // raw-channel volume meter. Variable length, unlike the final decoded
  // output's fixed channel count.
  void setChannelLoudness(std::vector<float> loudness) { channel_loudness_ = std::move(loudness); }
  const std::vector<float> & getChannelLoudness() const { return channel_loudness_; }

  // Legend for the raw-channel meter (e.g. "A0-A8 S") - computed in
  // Player.cpp, where the pre-padding regular channel count is known
  // unambiguously (getChannelLoudness()'s total size alone can't
  // distinguish e.g. a padded mono channel from an unpadded stereo pair -
  // both total 4 once SendA/SendB are appended).
  void setMeterLabel(std::string label) { meter_label_ = std::move(label); }
  const std::string & getMeterLabel() const { return meter_label_; }

  void setFFT(std::vector<float> data) { fft_data_ = std::move(data); }
  const std::vector<float> & getFFT() const { return fft_data_; }

  void setInfo(PlaybackInfo info) { info_ = std::move(info); }
  const PlaybackInfo & getInfo() const { return info_; }

private:
  std::vector<float> channel_loudness_;
  std::string meter_label_;
  std::vector<float> fft_data_;
  PlaybackInfo info_;
};

#endif
