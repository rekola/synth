#ifndef _TRACKSTATE_H_
#define _TRACKSTATE_H_

#include "TrackInfo.h"
#include "ActiveVoiceInfo.h"
#include "SampleData.h"
#include "ChannelConfiguration.h"
#include "SphericalPosition.h"
#include "AmbisonicEncoding.h"

#include <algorithm>
#include <vector>
#include <memory>
#include <unordered_map>

class Track;
class RenderContext;

class TrackState {
 public:
  explicit TrackState(const ChannelConfiguration & channel_config) : channel_config_(channel_config) { }
  virtual ~TrackState() { }
  
  // For rendering voices
  virtual SampleData render(int frames) {
    return renderChildren(frames, getChannelConfiguration());
  }

  // For rendering tracks
  virtual SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) {
    return renderChildren(frames, instruments, context, getChannelConfiguration());
  }

protected:
  // Gathers children into an accumulator of `accumulator_config`'s shape
  // rather than always this node's own getChannelConfiguration() - used
  // directly (not through the render() override above) by ReverbState/
  // CompressorState/DistortionState, which need to gather their children in
  // a narrower, guaranteed-real format (reduceForEffect) than their own
  // true (possibly ambisonic) format; every other node just gets this via
  // the public render() wrapper above, unchanged from today. Since
  // `accumulator_config` is never AMBISONIC for those two callers
  // (reduceForEffect's whole point), the mismatch-encode branch below
  // naturally never triggers for them either - their children are
  // constructed via getChildChannelConfiguration() with that exact same
  // reduced format (Effect.h/Reverb.h/etc.), so channel counts always
  // match and every child is just plain-mixed, same as pre-ambisonic code.
  SampleData renderChildren(int frames, const ChannelConfiguration & accumulator_config) {
    // Render every active child first, then decide the accumulator's shape
    // from what actually came back (hasChannel(Main/AuxA/AuxB)) rather
    // than predicting it up front - simpler than a separate non-rendering
    // "would this produce a channel" query, since the real answer is
    // sitting right there in each child's own rendered output. Main is
    // derived the same way AuxA/AuxB already are: absent only if *no*
    // child has it (e.g. every child's Send Main level is 0 this block),
    // matching how a leaf voice itself decides whether to allocate Main
    // at all - see InstrumentVoice::encodePosition().
    std::vector<SampleData> rendered;
    for (auto & [ id, child ] : getChildren()) {
      if (child->isActive()) {
	rendered.push_back(child->render(frames));
      }
    }

    bool has_main = false, has_aux_a = false, has_aux_b = false;
    for (auto & s : rendered) {
      has_main = has_main || s.hasChannel(Channel::Main);
      has_aux_a = has_aux_a || s.hasChannel(Channel::AuxA);
      has_aux_b = has_aux_b || s.hasChannel(Channel::AuxB);
    }
    SampleData data(has_main ? accumulator_config.numberOfChannels() : 0, has_aux_a, has_aux_b, frames);
    data.zero();

    // Every child now spatially encodes itself directly, using its own
    // position, to its own real (never reduced) ChannelConfiguration - see
    // InstrumentVoice::encodePosition() - so a child's rendered output
    // always already matches this accumulator's shape exactly; no
    // per-child dispatch is needed, just a plain mix.
    for (auto & s : rendered) data.mixNamed(s);

    return data;
  }

  SampleData renderChildren(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context, const ChannelConfiguration & accumulator_config) {
    std::vector<SampleData> rendered;
    for (auto & [ id, child ] : getChildren()) {
      rendered.push_back(child->render(frames, instruments, context));
    }

    bool has_main = false, has_aux_a = false, has_aux_b = false;
    for (auto & s : rendered) {
      has_main = has_main || s.hasChannel(Channel::Main);
      has_aux_a = has_aux_a || s.hasChannel(Channel::AuxA);
      has_aux_b = has_aux_b || s.hasChannel(Channel::AuxB);
    }
    SampleData sd(has_main ? accumulator_config.numberOfChannels() : 0, has_aux_a, has_aux_b, frames);
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

  virtual void clear() { children_.clear(); }

  virtual void playNote(float frequency, float velocity, int note_value) {
    for (auto & [ id, child ] : getChildren()) {
      child->playNote(frequency, velocity, note_value);
    }
  }

  virtual void stopNote() {
    for (auto & [ id, child ] : getChildren()) {
      child->stopNote();
    }
  }
  
  virtual void killNote() {
    for (auto & [ id, child ] : getChildren()) {
      child->killNote();
    }
  }

  // Reclaim a voice quickly without a hard cut - used by
  // InstrumentTrackState::retriggerVoices()/chokeExclusiveClasses() (see
  // their own comments) when a prior voice is either masked by a new
  // attack (same note identity) or must yield to SF2 exclusive-class
  // choking, neither of which should be audible as anything other than
  // the voice simply not being there any more. Default recurses into
  // children exactly like stopNote()/killNote() above, so a multi-region
  // SF2 group correctly cascades a fast release into every region child.
  // InstrumentVoice overrides this with a plain stopNote() (already
  // effectively instant for every non-SF2 leaf type - see its own
  // override of stopNote()); SoundFontVoice overrides it again with a
  // real short (~10ms) release through the existing envelope machinery,
  // reusing the same mechanism a normal release already uses, just
  // compressed - never an abrupt amplitude jump.
  virtual void fastRelease() {
    for (auto & [ id, child ] : getChildren()) {
      child->fastRelease();
    }
  }

  virtual bool isActive() const {
    for (auto & [ id, child ] : getChildren()) {
      if (child->isActive()) return true;
    }
    return false;
  }
  
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

  void applyAftertouch(float aftertouch) {
    aftertouch_ = aftertouch;
    
    for (auto & [ id, child ] : getChildren()) {
      child->applyAftertouch(aftertouch);
    }
  }

  float getAftertouch() const { return aftertouch_; }

  // Per-track-derived channel pressure (see InstrumentTrackState::
  // broadcastChannelPressure()) - separate from applyAftertouch/
  // getAftertouch above (which default to 1.0f and are read as a gain
  // multiplier by Tremolo/BiquadFilter/ResonantFilter). Recurses into
  // children exactly like applyAftertouch above does - required here
  // because a single played note is not always one leaf voice:
  // SoundFontInstrument::playNote() returns a plain (non-overriding)
  // TrackState group wrapping several SoundFontVoice children whenever
  // more than one region matches (stereo L/R sample pairs, velocity
  // layers - the common case for real GM patches), and
  // InstrumentTrackState only ever calls this on the top-level voice
  // stored in voices_, never reaching into a group's children itself.
  // Without this recursion the call silently no-ops on the group and
  // every SoundFontVoice inside it never learns of the pressure change.
  // Only SoundFontVoice overrides this (to drive SF2 channel-pressure
  // modulators) - every other leaf voice type simply inherits this
  // default and ignores channel pressure, the same "opt-in, no effect on
  // types that don't care" shape aftertouch's own 3 opt-in consumers
  // already have.
  virtual void applyChannelPressure(float pressure) {
    for (auto & [ id, child ] : getChildren()) {
      child->applyChannelPressure(pressure);
    }
  }
  virtual float getChannelPressure() const { return 0.0f; }

  const ChannelConfiguration & getChannelConfiguration() const { return channel_config_; }

  // Mutable access - only SongState::initialize() uses this, to push the
  // just-loaded Song's floor-reflection parameters (ChannelConfiguration.h)
  // into this already-constructed instance's stored copy, the same way
  // main.cpp's setAudioOutSampleRate()/setAmbisonicOrder() calls already
  // finalize one after construction. Every other TrackState is built
  // fresh per song load and never needs this.
  ChannelConfiguration & getMutableChannelConfiguration() { return channel_config_; }

  void addChild(int internal_id, std::unique_ptr<TrackState> child) { children_[internal_id] = std::move(child); }

  TrackState * getChildByInternalId(int id) {
    for (auto & [ child_id, child ] : getChildren()) {
      if (id == child_id) return child.get();
      auto r = child->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  const TrackState * getChildByInternalId(int id) const {
    for (auto & [ child_id, child ] : getChildren()) {
      if (id == child_id) return child.get();
      auto r = child->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  void removeChild(int id) {
    auto it = children_.find(id);
    if (it != children_.end()) children_.erase(it);
    else {
      for (auto & [ child_id, child ] : getChildren()) child->removeChild(id);
    }
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

  // Per-note-chain gain, composed down whatever wrapper chain a given
  // instrument happens to build - see getOwnLoudnessFactor(). Own factor
  // multiplies the loudest child, not the product of every child: for a
  // linear wrapper chain (e.g. EnvelopeFilterState wrapping one
  // OscilatorVoice) that's identical to a straight product, but
  // SoundFontInstrument groups multiple simultaneously-triggered sample
  // regions (velocity layers, stereo splits, ...) as siblings under a
  // plain TrackState - multiplying those together would compound each
  // region's own decay into an implausibly small number instead of
  // reflecting the note as a whole.
  float getLoudness() const {
    float max_child = 0.0f;
    bool has_children = false;
    for (auto & [ id, child ] : getChildren()) {
      has_children = true;
      max_child = std::max(max_child, child->getLoudness());
    }
    float own = getOwnLoudnessFactor();
    return has_children ? own * max_child : own;
  }

  // This node's own contribution to getLoudness() (1.0 = no opinion/pass
  // through). Overridden by InstrumentVoice (velocity-derived gain),
  // EnvelopeFilterState (envelope level), SoundFontVoice (velocity gain *
  // amp envelope level).
  virtual float getOwnLoudnessFactor() const { return 1.0f; }

  // The pattern note value (see Note::getValue()) this voice chain is
  // currently playing, or -1 if none is known. Searches children so a
  // wrapper node (e.g. an effect) transparently reports whatever its
  // instrument-voice descendant received via playNote().
  virtual int getNoteValue() const {
    for (auto & [ id, child ] : getChildren()) {
      int v = child->getNoteValue();
      if (v >= 0) return v;
    }
    return -1;
  }

  // Every distinct non-zero SF2 exclusive class (region.group, gen 57)
  // this voice chain's regions belong to - used by InstrumentTrackState::
  // chokeExclusiveClasses() to decide whether a new voice must choke an
  // existing one, regardless of note identity/pitch (two hi-hat regions
  // choke each other despite being different MIDI keys). Default unions
  // every child's own set, empty if none - correctly empty for every
  // non-SF2 instrument (nothing to override there) and correctly
  // aggregates across a multi-region SF2 group's children, which could in
  // principle carry different class values per region even though real
  // GM patches always give every region of one drum sound the same class.
  // Only SoundFontVoice overrides this (with its own single region's
  // class, if non-zero - 0 is the SF2 spec's "no class" sentinel).
  virtual std::vector<int> getExclusiveClasses() const {
    std::vector<int> classes;
    for (auto & [ id, child ] : getChildren()) {
      for (auto c : child->getExclusiveClasses()) classes.push_back(c);
    }
    return classes;
  }

  const std::unordered_map<int, std::unique_ptr<TrackState> > & getChildren() const { return children_; }
  std::unordered_map<int, std::unique_ptr<TrackState> > & getChildren() { return children_; }

  static inline float gainToDecibels(float gain) {
    return (gain <= .00001f ? -100.f : (float)(20.0 * log10(gain)));
  }

  static inline float decibelsToGain(float db) {
    return (db > -100.f ? powf(10.0f, db * 0.05f) : 0);
  }

protected:
  const TrackInfo & getTrackInfo() const { return track_info_; }
  void setTrackInfo(TrackInfo track_info) { track_info_ = std::move(track_info); }
  
  static inline float getRandF() {
    return (float)rand() / RAND_MAX;
  }
  
private:
  ChannelConfiguration channel_config_;
  std::unordered_map<int, std::unique_ptr<TrackState> > children_;
  float aftertouch_ = 1.0f;
  TrackInfo track_info_;
};

#endif
