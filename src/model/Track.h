#ifndef _TRACK_H_
#define _TRACK_H_

#include "StatefulSongObject.h"
#include "../state/VoiceState.h"
#include "TrackType.h"
#include "../ambisonic/SphericalPosition.h"
#include "SendLevels.h"
#include "NoteCoordinate.h"

#include <string_view>
#include <vector>
#include <memory>

class Track : public StatefulSongObject {
 public:
  Track(TrackType type) : type_(type) { }

  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override {
    return std::make_unique<TrackState>(config);
  }

  // The voice-chain counterpart to createState() above - reached only by
  // playNote()'s own default body below, never by createStateTree(). Every
  // leaf instrument (Oscillator/Noise/LFO/FileInstrument/
  // SoundFontInstrument/NoteMultiplier/GenericInstrument) overrides
  // playNote() itself directly and never reaches this; only Group and the
  // Effect family are genuinely usable both as a persistent track (via
  // createState()/createStateTree()) and inside an instrument definition
  // (via this) - see plans/trackstate-voicestate-split.md. Default mirrors
  // createState()'s own plain-passthrough default, which is exactly
  // correct for Group (a plain fan-out wrapper in either role); each
  // Effect subclass overrides this the same way it already overrides
  // createState().
  virtual std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & config) const {
    return std::make_unique<VoiceState>(config);
  }

  // What format this node's children should be constructed with, given the
  // format this node itself was asked to produce. Default: passthrough -
  // matches every node's actual behavior except the few effects (Reverb,
  // Compressor, Distortion) whose DSP genuinely needs real stereo width or
  // is nonlinear; those override this to reduceForEffect(config). Consulted
  // by both createStateTree() and the default playNote() body below, since
  // an effect can be reached either way.
  virtual ChannelConfiguration getChildChannelConfiguration(const ChannelConfiguration & config) const { return config; }

  // The default physical half-width (meters) a track resolves to when its
  // own InstrumentTrack::extent_ wasn't explicitly authored - see
  // SphericalPosition::extent's own doc comment. Default: delegate to the
  // first child, same "passthrough unless a leaf overrides it" shape as
  // getChildChannelConfiguration() above - covers every wrapper Track
  // (NoteMultiplier, EnvelopeFilter, ResonantFilter, ...) for free, so a
  // real leaf instrument only needs to override this when it actually has
  // a nonzero default (SoundFontInstrument; GenericInstrument forwards to
  // whatever it resolves to). A true leaf with no children (Oscillator,
  // Noise, LFO, FileInstrument) falls through to 0 - a point source,
  // unless the artist sets an explicit extent on the track.
  virtual float getDefaultExtent() const {
    return getChildren().empty() ? 0.0f : getChildren()[0]->getDefaultExtent();
  }

  std::unique_ptr<TrackState> createStateTree(const ChannelConfiguration & config) const {
    auto state = createState(config);
    auto child_config = getChildChannelConfiguration(config);
    for (auto & child : getChildren()) {
      state->addChild(child->getInternalId(), child->createStateTree(child_config));
    }
    return state;
  }

  TrackState & getState(TrackState & parent_state) {
    auto state = parent_state.getChildByInternalId(getInternalId());
    if (!state) {
      auto new_state = createStateTree(parent_state.getChannelConfiguration());
      state = new_state.get();
      parent_state.addChild(getInternalId(), std::move(new_state));
    }
    return *state;
  }

  virtual const char * getElementName() const = 0;

  // note_coord: a stable per-note coordinate (NoteCoordinate.h), fed to
  // HashField by any leaf/wrapper that needs a reproducible-per-note
  // jitter value (NoteMultiplier's unison/detune spread, TapeDegradation's
  // per-instance seed, ...) - not otherwise used by playNote() itself.
  // Defaulted to {} here (and only here - no override repeats the
  // default) so every existing call site that doesn't care about
  // reproducible randomization keeps compiling unchanged; a caller that
  // does care (InstrumentTrackState::noteOn(), ArpeggiatorState's
  // stepper) passes a real one explicitly. Every override that recurses
  // into children must forward whatever it received rather than letting
  // the default reintroduce itself partway down the tree - see this
  // default body's own forward below.
  virtual std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}) const {
    auto group = createVoiceState(config);
    auto child_config = getChildChannelConfiguration(config);
    for (auto & child : getChildren()) {
      auto voice = child->playNote(child_config, position, frequency, detune, velocity, note_value, sends, note_coord);
      if (voice.get()) group->addChild(child->getInternalId(), std::move(voice));
    }
    return group;
  }

  TrackType getType() const { return type_; }

  const Track & getChild(int i) const { return *(children_[static_cast<size_t>(i)]); }
  Track & getChild(int i) { return *(children_[static_cast<size_t>(i)]); }
  
  Track & addChild(std::unique_ptr<Track> track) { children_.push_back(std::move(track)); return *(children_.back()); }

  std::vector<std::unique_ptr<Track> > & getChildren() { return children_; }
  const std::vector<std::unique_ptr<Track> > & getChildren() const { return children_; }

  int getDepth() const {
    int max_depth = 0;
    for (auto & child : getChildren()) {
      auto d = child->getDepth();
      if (d > max_depth) max_depth = d;
    }
    return 1 + max_depth;
  }

  const Track * getChildByInternalId(int id) const {
    if (getInternalId() == id) return this;
    for (auto & child : getChildren()) {
      auto r = child->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getChildByInternalId(int id) {
    if (getInternalId() == id) return this;
    for (auto & child : getChildren()) {
      auto r = child->getChildByInternalId(id);
      if (r) return r;
    }
    return nullptr;
  }

  // Removes the direct or indirect child track whose internal id is `id` -
  // same self-then-children search shape as getChildByInternalId() above,
  // but erasing rather than locating. `id` matching `this` itself is
  // Song::removeTrack()'s own job (it owns the top-level tracks_ vector
  // this class has no access to), not handled here.
  bool removeChildByInternalId(int id) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
      if ((*it)->getInternalId() == id) {
	children_.erase(it);
	return true;
      }
    }
    for (auto & child : children_) {
      if (child->removeChildByInternalId(id)) return true;
    }
    return false;
  }

  const Track * getChildById(std::string_view id) const {
    if (getId() == id) return this;
    for (auto & child : getChildren()) {
      auto r = child->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }

  Track * getChildById(std::string_view id) {
    if (getId() == id) return this;
    for (auto & child : getChildren()) {
      auto r = child->getChildById(id);
      if (r) return r;
    }
    return nullptr;
  }
  
 private:
  TrackType type_;
  std::vector<std::unique_ptr<Track> > children_;
};

#endif
