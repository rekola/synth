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
  // many are active, then always AuxA/AuxB last) - feeds the UI's
  // raw-channel volume meter. Variable length, unlike the final decoded
  // output's fixed channel count.
  void setChannelLoudness(std::vector<float> loudness) { channel_loudness_ = std::move(loudness); }
  const std::vector<float> & getChannelLoudness() const { return channel_loudness_; }

  // Legend for the raw-channel meter (e.g. "M1-9 A") - computed in
  // Player.cpp, where the pre-padding regular channel count is known
  // unambiguously (getChannelLoudness()'s total size alone can't
  // distinguish e.g. a padded mono channel from an unpadded stereo pair -
  // both total 4 once AuxA/AuxB are appended).
  void setMeterLabel(std::string label) { meter_label_ = std::move(label); }
  const std::string & getMeterLabel() const { return meter_label_; }

  void setInfo(PlaybackInfo info) { info_ = std::move(info); }
  const PlaybackInfo & getInfo() const { return info_; }

private:
  std::vector<float> channel_loudness_;
  std::string meter_label_;
  PlaybackInfo info_;
};

#endif
