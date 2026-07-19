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
    SampleData data(accumulator_config, frames);
    data.zero();

    bool is_ambisonic = accumulator_config.getType() == ChannelConfiguration::AMBISONIC;
    for (auto & [ id, child ] : getChildren()) {
      if (child->isActive()) {
	auto s = child->render(frames);
	// A child narrower than this node's own (ambisonic) format bottomed
	// out at MONO without anything re-encoding it back up (see
	// reduceForPositionalGroup, AmbisonicEncoding.h) - FOA-encode it
	// here using its own position rather than blindly mixing, which is
	// what lets NoteMultiplier/SoundFontInstrument's differently-
	// positioned sub-voices (and a single voice under a transparent
	// per-voice effect) spatialize correctly with no ambisonic-specific
	// code of their own. If channel counts already match, the child
	// already produced genuine ambisonic-shaped output (e.g. a nested
	// composite that went through this same logic one level down) -
	// mix it directly, same as always.
	if (is_ambisonic && s.numberOfChannels() < data.numberOfChannels()) {
	  if (!s.isZero()) {
	    positional_mixer_.encode(child.get(), s, child->getPosition(), data);
	    data.setNonZero();
	  }
	} else {
	  data.mix(s);
	}
      } else {
	positional_mixer_.remove(child.get());
      }
    }

    return data;
  }

  SampleData renderChildren(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context, const ChannelConfiguration & accumulator_config) {
    SampleData sd(accumulator_config, frames);
    sd.zero();

    bool child_has_solo = false, active = false;
    for (auto & [ id, child ] : getChildren()) {
      auto s = child->render(frames, instruments, context);
      if (s.isSolo() && !child_has_solo) {
	child_has_solo = true;
	sd.zero();
	sd.setSolo(true);
      }
      if (s.isSolo() || !child_has_solo) {
	sd.mix(s);
	active = true;
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

  const ChannelConfiguration & getChannelConfiguration() const { return channel_config_; }

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

  // This voice chain's position in space, for external ambisonic encoding
  // (see AmbisonicEncoding.h). Same "search children, return the first
  // found" idiom as getNoteValue() - a wrapper node (composite instrument,
  // per-voice effect) transparently reports whatever its InstrumentVoice
  // descendant was constructed with. Overridden by InstrumentVoice (returns
  // its stored position_) and further by SoundFontVoice (folds in the
  // region's own pan).
  virtual SphericalPosition getPosition() const {
    for (auto & [ id, child ] : getChildren()) {
      auto p = child->getPosition();
      if (p.distance > 0.0f) return p;
    }
    return SphericalPosition{};
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
  PositionalMixer positional_mixer_;
};

#endif
