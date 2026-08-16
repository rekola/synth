#ifndef _VOICESTATE_H_
#define _VOICESTATE_H_

#include "TreeNode.h"
#include "AudioBuffer.h"
#include "ChannelConfiguration.h"

#include <algorithm>
#include <vector>
#include <memory>

// Root of the ephemeral, per-note voice chain (as opposed to TrackState,
// the persistent, per-block-rendered track tree) - see
// plans/trackstate-voicestate-split.md for the full rationale. Built fresh
// by Track::playNote() for every note-on (Oscillator/Noise/LFO/
// FileInstrument/SoundFontInstrument/NoteMultiplier/GenericInstrument each
// construct their own leaf/group VoiceState directly; Group and the
// Effect family - the only Track subclasses genuinely usable both as a
// persistent track and inside an instrument definition - reach this via
// Track::createVoiceState()), owned by InstrumentTrackState::voices_, and
// discarded once the note finishes (InstrumentTrackState::
// clearFinishedVoices()). Every method below only ever matters for that
// one note's lifetime - nothing here is track-wide state (track_info_,
// mute/solo, ...; see TrackState for that).
class VoiceState : public TreeNode<VoiceState> {
 public:
  explicit VoiceState(const ChannelConfiguration & channel_config) : TreeNode(channel_config) { }

  virtual AudioBuffer render(int frames) {
    return renderChildren(frames, getChannelConfiguration());
  }

 protected:
  // Gathers children into an accumulator of `accumulator_config`'s shape
  // rather than always this node's own getChannelConfiguration() - used
  // directly (not through the render() override above) by ChorusVoiceState/
  // DistortionVoiceState, which need to gather their children in a
  // narrower, guaranteed-real format (reduceForEffect) than their own true
  // (possibly ambisonic) format; every other node just gets this via the
  // public render() wrapper above, unchanged from today. Since
  // `accumulator_config` is never AMBISONIC for those two callers
  // (reduceForEffect's whole point), the mismatch-encode branch below
  // naturally never triggers for them either - their children are
  // constructed via getChildChannelConfiguration() with that exact same
  // reduced format (Effect.h/Chorus.h/Distortion.h), so channel counts
  // always match and every child is just plain-mixed, same as
  // pre-ambisonic code.
  AudioBuffer renderChildren(int frames, const ChannelConfiguration & accumulator_config) {
    // Render every active child first, then decide the accumulator's shape
    // from what actually came back (hasChannel(Main/AuxA/AuxB)) rather
    // than predicting it up front - simpler than a separate non-rendering
    // "would this produce a channel" query, since the real answer is
    // sitting right there in each child's own rendered output. Main is
    // derived the same way AuxA/AuxB already are: absent only if *no*
    // child has it (e.g. every child's Send Main level is 0 this block),
    // matching how a leaf voice itself decides whether to allocate Main
    // at all - see InstrumentVoice::encodePosition().
    std::vector<AudioBuffer> rendered;
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
    AudioBuffer data(has_main ? accumulator_config.numberOfChannels() : 0, has_aux_a, has_aux_b, frames);
    data.zero();

    // Every child now spatially encodes itself directly, using its own
    // position, to its own real (never reduced) ChannelConfiguration - see
    // InstrumentVoice::encodePosition() - so a child's rendered output
    // always already matches this accumulator's shape exactly; no
    // per-child dispatch is needed, just a plain mix.
    for (auto & s : rendered) data.mixNamed(s);

    return data;
  }

 public:
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
  // VoiceState group wrapping several SoundFontVoice children whenever
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

  // 2Lxx/2Rxx azimuth slide (Command::isAzimuthSlide(), consumed by
  // InstrumentTrackState::render()'s chunked loop) - nudges this voice
  // chain's own spatial position live, mid-note. Default recurses into
  // children for the same reason applyChannelPressure() above does: a
  // multi-region SoundFontInstrument group is a plain, non-overriding
  // VoiceState wrapping several real leaf voices (see that method's own
  // comment), so the recursion is what actually reaches them. Only
  // InstrumentVoice overrides this (every leaf voice type derives from
  // it - see InstrumentVoice.h).
  virtual void adjustAzimuth(float delta) {
    for (auto & [ id, child ] : getChildren()) {
      child->adjustAzimuth(delta);
    }
  }

  // Send Main/A/B (InstrumentTrackState::setSendMain()/setSendA()/
  // setSendB(), the Launchpad/UI Send knobs) pushed to every already-
  // sounding voice, not just future notes - unlike adjustAzimuth() above,
  // these carry an absolute value rather than a delta (there's no
  // tick-scheduled slide command for sends). Default recursion and the
  // "only InstrumentVoice overrides this" shape are otherwise identical to
  // adjustAzimuth() above, for the same reason.
  virtual void adjustSendMain(float s) {
    for (auto & [ id, child ] : getChildren()) {
      child->adjustSendMain(s);
    }
  }
  virtual void adjustSendA(float s) {
    for (auto & [ id, child ] : getChildren()) {
      child->adjustSendA(s);
    }
  }
  virtual void adjustSendB(float s) {
    for (auto & [ id, child ] : getChildren()) {
      child->adjustSendB(s);
    }
  }

  // Per-note-chain gain, composed down whatever wrapper chain a given
  // instrument happens to build - see getOwnLoudnessFactor(). Own factor
  // multiplies the loudest child, not the product of every child: for a
  // linear wrapper chain (e.g. EnvelopeFilterVoiceState wrapping one
  // OscillatorVoice) that's identical to a straight product, but
  // SoundFontInstrument groups multiple simultaneously-triggered sample
  // regions (velocity layers, stereo splits, ...) as siblings under a
  // plain VoiceState - multiplying those together would compound each
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
  // EnvelopeFilterVoiceState (envelope level), SoundFontVoice (velocity
  // gain * amp envelope level).
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

 private:
  float aftertouch_ = 1.0f;
};

#endif
