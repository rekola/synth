#ifndef _TRACKSTATE_H_
#define _TRACKSTATE_H_

#include "TreeNode.h"
#include "TrackInfo.h"
#include "ActiveVoiceInfo.h"
#include "AudioBuffer.h"
#include "ChannelConfiguration.h"
#include "SphericalPosition.h"
#include "AmbisonicEncoding.h"

#include <algorithm>
#include <vector>
#include <memory>
#include <unordered_map>

class Track;
class RenderContext;

// Root of the persistent, per-block-rendered track tree - one TrackState
// per Track in song.getTracks(), built once by Track::createStateTree()
// and walked every audio block by SongState::renderBlock(). See VoiceState.h
// (the ephemeral, per-note voice-chain counterpart) and
// plans/trackstate-voicestate-split.md for the full split rationale -
// every method here only ever matters for a persistent track-tree node
// (mute/solo, track_id/instrument_id, TrackInfo for the UI, ...); note
// on/off, aftertouch, per-voice send/azimuth pushes, and everything else
// scoped to a single note's lifetime lives on VoiceState instead.
class TrackState : public TreeNode<TrackState> {
 public:
  explicit TrackState(const ChannelConfiguration & channel_config) : TreeNode(channel_config) { }

  virtual AudioBuffer render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) {
    return renderChildren(frames, instruments, context, getChannelConfiguration());
  }

protected:
  AudioBuffer renderChildren(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context, const ChannelConfiguration & accumulator_config) {
    std::vector<AudioBuffer> rendered;
    for (auto & [ id, child ] : getChildren()) {
      rendered.push_back(child->render(frames, instruments, context));
    }

    bool has_main = false, has_aux_a = false, has_aux_b = false;
    for (auto & s : rendered) {
      has_main = has_main || s.hasChannel(Channel::Main);
      has_aux_a = has_aux_a || s.hasChannel(Channel::AuxA);
      has_aux_b = has_aux_b || s.hasChannel(Channel::AuxB);
    }
    AudioBuffer sd(has_main ? accumulator_config.numberOfChannels() : 0, has_aux_a, has_aux_b, frames);
    sd.zero();

    bool child_has_solo = false;
    for (auto & s : rendered) {
      if (s.isSolo() && !child_has_solo) {
	child_has_solo = true;
	sd.zero();
	sd.setSolo(true);
      }
      if (s.isSolo() || !child_has_solo) {
	sd.mixNamed(s);
      }
    }

    return sd;
  }

public:

  virtual void clear() { getChildren().clear(); }

  virtual bool isActive() const {
    for (auto & [ id, child ] : getChildren()) {
      if (child->isActive()) return true;
    }
    return false;
  }

  // Sums this track subtree's own voices (via InstrumentTrackState's
  // override, which additionally walks its voices_) with every descendant
  // track's own count - the SongState root's own call (Player.cpp) is
  // what PlaybackInfo::getVoiceCount() ultimately reports to the UI.
  virtual int getVoiceCount() const {
    int n = 0;
    if (isActive()) n++;
    for (auto & [ id, child ] : getChildren()) {
      n += child->getVoiceCount();
    }
    return n;
  }

  virtual int getAllocatedVoiceCount() const {
    int n = 1;
    for (auto & [ id, child ] : getChildren()) {
      n += child->getAllocatedVoiceCount();
    }
    return n;
  }

  void getAllTrackInfo(std::unordered_map<int, TrackInfo> & info) const {
    for (auto & [ id, child ] : getChildren()) {
      info[id] = child->getTrackInfo();
      child->getAllTrackInfo(info);
    }
  }

  // Per-track lists of currently-sounding (note_value, loudness) pairs, for
  // LED/UI feedback (e.g. LaunchpadManager). Default recurses into children;
  // InstrumentTrackState overrides to report its own voices_.
  virtual void getAllActiveVoices(std::unordered_map<int, std::vector<ActiveVoiceInfo> > & voices) const {
    for (auto & [ id, child ] : getChildren()) {
      child->getAllActiveVoices(voices);
    }
  }

protected:
  const TrackInfo & getTrackInfo() const { return track_info_; }
  void setTrackInfo(TrackInfo track_info) { track_info_ = std::move(track_info); }

private:
  TrackInfo track_info_;
};

#endif
