#ifndef _LEAFTRACKSTATE_H_
#define _LEAFTRACKSTATE_H_

#include "TrackState.h"
#include "VoiceState.h"
#include "../audio/AudioBuffer.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SendLevels.h"

#include <algorithm>

// The runtime counterpart of LeafTrack (src/model/LeafTrack.h) - mute/solo/
// position/sends/voices_ bookkeeping every leaf track type shares
// (InstrumentTrackState, SampleTrackState), independent of what actually
// produces those voices. Mirrors the model-layer LeafTrack/InstrumentTrack/
// SampleTrack split: a plain TrackState has no notion of "leaf" at all,
// and InstrumentTrackState builds on this with the pool-instrument
// resolution and note-column/chord/pressure bookkeeping only a
// pattern-driven, pitched track needs - SampleTrackState has no use for any
// of that, just this class's plain single-voice-per-trigger machinery.
class LeafTrackState : public TrackState {
public:
  explicit LeafTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, const SphericalPosition & position, const SendLevels & sends)
    : TrackState(channel_config), solo_(solo), muted_(muted), track_id_(track_id), position_(position), sends_(sends) { }

  void addVoice(int column, std::unique_ptr<VoiceState> voice) {
    voices_[column].push_back(std::move(voice));
  }

  // Combines this track's own currently-sounding voices_ into one buffer -
  // not an override of anything on TrackState (which has no voice-shaped
  // render() any more - voices_ is VoiceState-typed, not TrackState-typed;
  // see plans/trackstate-voicestate-split.md); named distinctly from
  // TrackState::render's 3-arg overload (rather than overloading render()
  // itself) so the two can't be mistaken for one hiding the other. Both
  // InstrumentTrackState::render() and SampleTrackState::render() call this
  // by unqualified name once per pending-event sub-chunk, and
  // ArpeggiatorState overrides it (see its own doc comment) to interleave
  // its stepper's timing with that same chunking, purely via ordinary
  // virtual dispatch.
  virtual AudioBuffer renderVoices(int frames) {
    // Render every active voice first (still calling render() even when
    // muted, so envelopes/LFOs keep advancing - only mixing is skipped),
    // then decide this track's own accumulator shape from what actually
    // came back rather than a separate non-rendering prediction.
    std::vector<AudioBuffer> rendered;
    bool is_active = false;

    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) {
	  auto s = voice->render(frames);
	  is_active = true;
	  if (!isMuted()) rendered.push_back(std::move(s));
	}
      }
    }

    bool has_main = false, has_aux_a = false, has_aux_b = false;
    for (auto & s : rendered) {
      has_main = has_main || s.hasChannel(Channel::Main);
      has_aux_a = has_aux_a || s.hasChannel(Channel::AuxA);
      has_aux_b = has_aux_b || s.hasChannel(Channel::AuxB);
    }
    AudioBuffer data(has_main ? getChannelConfiguration().numberOfChannels() : 0, has_aux_a, has_aux_b, frames, isSolo());
    data.zero();

    // Every voice now spatially encodes itself directly, using its own
    // position, to its own real (never reduced) ChannelConfiguration - see
    // InstrumentVoice::encodePosition() - so a voice's rendered output
    // always already matches this accumulator's shape exactly; no
    // per-voice dispatch is needed, just a plain mix.
    for (auto & s : rendered) data.mixNamed(s);

    setTrackInfo(TrackInfo( is_active, data.isClipping() ));

    return data;
  }

  void stopVoices(int column) {
    auto it = voices_.find(column);
    if (it != voices_.end()) {
      for (auto & voice : it->second) if (voice->isActive()) voice->stopNote();
    }
  }

  // stopVoices()'s own natural release (voice->stopNote(), full authored
  // release tail - never fastRelease()'s short ~10ms envelope, that's for
  // inaudibly reclaiming a voice under a fresh attack, not for a musical
  // stop), just generalized to every column at once rather than one at a
  // time - used when a track's pattern/clip is pulled out from under it
  // (Launchpad Session view's "stop this track") and there's no single
  // column to target, unlike an ordinary per-column stop.
  virtual void stopAllVoices() {
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->stopNote();
    }
  }

  void clear() override {
    TrackState::clear();
    voices_.clear();
  }

  bool isActive() const override {
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) return true;
      }
    }
    return false;
  }

  int getVoiceCount() const override {
    int n = TrackState::getVoiceCount();
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	n += voice->getVoiceCount();
      }
    }
    return n;
  }

  int getAllocatedVoiceCount() const override {
    int n = TrackState::getAllocatedVoiceCount();
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	n += voice->getAllocatedVoiceCount();
      }
    }
    return n;
  }

  void getAllActiveVoices(std::unordered_map<int, std::vector<ActiveVoiceInfo> > & out) const override {
    std::vector<ActiveVoiceInfo> own;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) {
	if (voice->isActive()) own.push_back({ voice->getNoteValue(), voice->getLoudness() });
      }
    }
    if (!own.empty()) out[track_id_] = std::move(own);
    TrackState::getAllActiveVoices(out);
  }

  // Live control changes, pushed from the UI thread via PlaybackControlEvent
  // (SET_TRACK_MUTED/SOLO/SEND_A/SEND_B/SEND_MAIN - see Player::handleEvent) and
  // applied directly to this already-running state, unlike the constructor
  // argument above which only seeds the initial value at song load.
  bool isMuted() const { return muted_; }
  void setMuted(bool m) { muted_ = m; }

  bool isSolo() const { return solo_; }
  void setSolo(bool s) { solo_ = s; }

  // Send Main/A/B all push into every already-active voice too, not just
  // future notes/triggers (same reasoning as setAzimuth()/adjustAzimuth()
  // below: sends_ isn't read fresh from anywhere but this voice's own
  // construction otherwise). Reuses the same
  // VoiceState::adjust*() virtual-recursion mechanism adjustAzimuth() does
  // (so a multi-region SoundFontInstrument group's real leaf voices are all
  // reached too), just carrying an absolute value instead of a per-tick
  // delta - there's no tick-scheduled slide command for sends the way
  // there is for azimuth, these are live knobs (Launchpad/UI Send rows),
  // not a pattern effect. See VoiceState::adjustSendMain()/adjustSendA()/
  // adjustSendB().
  void setSendMain(float s) {
    sends_.main = s;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustSendMain(s);
    }
  }
  void setSendA(float s) {
    sends_.a = s;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustSendA(s);
    }
  }
  void setSendB(float s) {
    sends_.b = s;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustSendB(s);
    }
  }

  // The live-knob path (Launchpad/UI Pan row, via Controller::
  // setTrackAzimuth()) - reaches every already-active voice too, not just
  // future triggers, by reusing adjustAzimuth() below with the absolute-to-
  // delta conversion done here.
  void setAzimuth(float a) { adjustAzimuth(a - position_.azimuth); }
  float getAzimuth() const { return position_.azimuth; }

  // Shared by the live Pan-row knob above and the 0Hxx/0Kxx azimuth slide
  // (Command::isAzimuthSlide(), scheduled per-tick by SongState::
  // scheduleAzimuthSlide(), consumed by InstrumentTrackState::render()'s own
  // chunked loop) - both are just different sources of a delta that should
  // audibly move whatever is currently sounding, not only future notes.
  // VoiceState::adjustAzimuth()'s default recursion (overridden by
  // InstrumentVoice - see its own comment) makes this correct even for a
  // multi-region SoundFontInstrument group. No longer an override of
  // anything on TrackState (azimuth is a VoiceState-only concept).
  void adjustAzimuth(float delta) {
    position_.azimuth += delta;
    for (auto & [ column, voices ] : voices_) {
      for (auto & voice : voices) if (voice->isActive()) voice->adjustAzimuth(delta);
    }
  }

protected:
  // Read access to this track's own position/sends/id for a subclass that
  // needs to construct its own voices directly (e.g. ArpeggiatorState
  // triggering a step, SampleTrackState::triggerClip()) rather than through
  // the normal pending-events path, which already has position_/sends_ in
  // scope. Mirrors getChannelConfiguration()'s existing public accessor for
  // the same otherwise-private-to-this-class piece of construction state.
  const SphericalPosition & getPosition() const { return position_; }
  const SendLevels & getSends() const { return sends_; }
  int getTrackId() const { return track_id_; }

  static inline bool is_not_playing(const std::unique_ptr<VoiceState> & voice) { return !voice->isActive(); }

  void clearFinishedVoices() {
    for (auto & [ id, voices ] : voices_) {
      voices.erase(std::remove_if(voices.begin(), voices.end(), is_not_playing), voices.end());
    }
  }

  // Every voice this track currently owns, keyed by column - a single note
  // column for a plain InstrumentTrackState note-on, always column 0 for a
  // SampleTrackState clip (a SampleTrack only ever plays one clip at a
  // time). Protected, not private: InstrumentTrackState's own note-column/
  // chord/pressure/exclusive-class bookkeeping (retriggerVoices(),
  // chokeExclusiveClasses(), pushChannelPressureToVoices()) iterates it
  // directly.
  std::unordered_map<int, std::vector<std::unique_ptr<VoiceState> > > voices_;

private:
  bool solo_, muted_;
  int track_id_;
  SphericalPosition position_;
  SendLevels sends_;
};

#endif
