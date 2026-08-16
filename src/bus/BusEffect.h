#ifndef _BUSEFFECT_H_
#define _BUSEFFECT_H_

#include "../ambisonic/AmbisonicEncoding.h"
#include "../ambisonic/SphericalPosition.h"
#include "../model/SongObject.h"

#include <vector>

// Common base for the shared send bus's effects (SendBusProcessor owns two
// of these, one per slot, both fed by a cross-track SendA/SendB mono sum
// and persisting for the whole playback session - unlike a regular
// per-track Effect, which is created fresh per track/note).
//
// Standardizes construction (sample rate), the "always process every
// block, even when the mono input is silent" contract every bus effect
// needs so its own internal tail/modulation state stays continuous across
// blocks, and everything SendBusProcessor's generic 2-slot loop needs from
// any occupant: how many spatial taps it produces (getNumTaps()/getTap()/
// getTapDirection()), the pre-encode sum used for a chain send
// (getChainSendSum() - universal, not per-type; see BusEffectRegistry.h),
// and its own return level and chain-send ratio (getWetLevel()/
// getChainSendLevel()) - properties of this effect instance, not of some
// separate slot wrapper, since a BusEffect only ever occupies one slot for
// its whole life.
//
// Inherits SongObject's loadParameters()/storeParameters() rather than
// routing parameter (de)serialization through the registry
// (BusEffectRegistry.h). The generic wet/chainSend attributes every
// BusEffect has (below) are handled once, here, in this base class - a
// concrete effect type only overrides loadParameters()/storeParameters()
// for its own type-specific parameters (FDNReverb::setParameters(), etc.),
// calling BusEffect::loadParameters()/storeParameters() first, the same
// chaining convention every other SongObject subclass already uses
// (effects/Distortion.cpp calls Effect::loadParameters() first, etc.) - the
// registry's (BusEffectRegistry.h) only job is picking *which* concrete
// type to construct from an XML element name.
class BusEffect : public SongObject {
 public:
  static constexpr float kDefaultWetLevel = 0.3f;
  static constexpr float kDefaultChainSendLevel = 0.3f;

  // defaultWetLevel/defaultChainSendLevel: this concrete instance's own
  // tuned "unchanged" values (FDNReverb passes 0.2512 for wet; delay
  // passes 0.354 - see each class's own constructor) - supplied once here,
  // as a real constructor argument, rather than inferred from "whichever
  // value gets set first" (a fragile convention this deliberately avoids).
  // loadParameters()/storeParameters() below read/compare against these,
  // so no concrete type needs to reimplement wet/chainSend handling
  // itself; a type happy with the base class's own default just doesn't
  // pass anything for these.
  explicit BusEffect(int sampleRate, float defaultWetLevel = kDefaultWetLevel, float defaultChainSendLevel = kDefaultChainSendLevel)
    : sampleRate_(sampleRate),
      wet_level_(defaultWetLevel), chain_send_level_(defaultChainSendLevel),
      default_wet_level_(defaultWetLevel), default_chain_send_level_(defaultChainSendLevel) { }
  virtual ~BusEffect() = default;

  // Always runs, even for silent input, so internal tail/feedback/
  // modulation state stays continuous across blocks.
  virtual void process(const float * monoInput, int frames) = 0;

  virtual int getNumTaps() const = 0;
  virtual const float * getTap(int i) const = 0;
  virtual SphericalPosition getTapDirection(int i) const = 0;

  // Pre-encode mono sum of this effect's current-block tap outputs - what
  // a chain send (see getChainSendLevel()) actually forwards to the next
  // slot. Default (BusEffect.cpp) sums every tap via getTap()/
  // getNumTaps(); override only if an effect's output isn't well-modeled
  // as N independent taps.
  virtual void getChainSendSum(float * out, int frames) const;

  // Return level: gain applied when this effect's taps are encoded into
  // the shared ambisonic bus (the final mix) - independent of how much of
  // it is additionally chained forward, below.
  float getWetLevel() const { return wet_level_; }
  void setWetLevel(float w) { wet_level_ = w; }

  // Chain-send ratio: 0.0 = none of getChainSendSum() reaches the next
  // effect in the chain, 1.0 = the full sum reaches it completely
  // unscaled (unity - no headroom loss, no extra gain). Every BusEffect
  // has this uniformly; whichever effect sits in slot A has it exist but
  // unread, since nothing sits after A. Default (kDefaultChainSendLevel,
  // 0.3, not 0.0): an effect sitting in slot B with nothing at all
  // reaching slot A is the unlikely case, not the expected one
  // (concretely: the default bus has delay in B feeding reverb in A, so
  // "some of the delay's output gets a bit of reverb on it" should be
  // true out of the box, not an opt-in).
  float getChainSendLevel() const { return chain_send_level_; }
  void setChainSendLevel(float c) { chain_send_level_ = c; }

  // Generic "wet"/"chainSend" attributes, deviation-only against this
  // instance's own default_wet_level_/default_chain_send_level_ (set at
  // construction, above) - handled once, here, so no concrete type needs
  // to touch either. A concrete type's own loadParameters()/
  // storeParameters() override calls these first (SongObject's usual
  // chaining convention), then handles its own type-specific parameters.
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  // Per-tap gain-interpolation state, one per getNumTaps() - lazily sized
  // on first use (BusEffect.cpp) rather than in the constructor, since
  // getNumTaps() is virtual and not yet safely callable during BusEffect's
  // own construction.
  AmbisonicVoiceEncoder & getTapEncoder(int i);

  // Called once at song load (safe to call again) with the song's current
  // tempo resolved to seconds-per-row - most effects have no use for this
  // (default no-op); MultiTapDelay overrides it, since its tap lengths are
  // expressed in pattern rows, not raw seconds, and row duration isn't a
  // stored/deviating parameter the way baseRows/feedback/damping are (it
  // comes from the song's own tempo, not from this slot's own XML
  // attributes) - see SongState::initialize()'s load-time wiring.
  virtual void setRowDuration(float rowDurationSeconds) { }

  // Second, parallel output path for effects whose output isn't a set of
  // point-source taps - see plans/drum-bus-saturator.md. The tap
  // path above (getNumTaps()/getTap()/getTapDirection(), consumed by
  // SendBusProcessor via computeAmbisonicGains()) can only ever produce
  // one mono signal broadcast into every channel at fixed relative gains
  // for a single direction - coherent by construction, since
  // computeAmbisonicGains() always gives channel 0 (W) the same gain
  // regardless of direction. An effect that needs genuinely different,
  // decorrelated content per channel (AmbisonicDiffuseEncoder) can't be
  // expressed that way at all, so it writes directly into busAmbisonic's
  // regular channels here instead, ADDED (not overwritten - SendBusProcessor
  // zeroes its accumulator once before either slot runs), applying this
  // instance's own getWetLevel() itself, since the generic tap loop (which
  // applies wet uniformly) never runs for these channels. Default no-op:
  // every existing BusEffect (FDNReverb/MultiTapDelay/GranularCloud/
  // NullBusEffect) is a pure tap producer and needs nothing here.
  virtual void encodeDirect(AudioBuffer & busAmbisonic, int frames) { }

 protected:
  int getSampleRate() const { return sampleRate_; }

 private:
  int sampleRate_;
  float wet_level_;
  float chain_send_level_;
  float default_wet_level_;
  float default_chain_send_level_;
  std::vector<AmbisonicVoiceEncoder> tap_encoders_;
};

#endif
