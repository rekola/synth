#ifndef _SENDBUSPROCESSOR_H_
#define _SENDBUSPROCESSOR_H_

#include "../SampleData.h"
#include "../ChannelConfiguration.h"
#include "../AmbisonicEncoding.h"
#include "BusEffect.h"

#include <array>
#include <memory>
#include <vector>

// Owned by SongState (one per playback session, persisting across every
// block - see SongState.h): a generic 2-slot effect chain, fed by the
// cross-track AuxA (slot A's input) and AuxB (slot B's input) sums.
// Which concrete BusEffect occupies each slot is resolved once, at song
// load, from the project file (or the compiled-in default: A = reverb,
// B = delay) via bus/BusEffectRegistry.h - see setSlotEffect() and
// SongState::initialize(). Slot occupancy never changes for the lifetime
// of a loaded song (no runtime reconfiguration, no teardown/crossfade
// machinery - see the load-time-only slot-configuration plan this
// implements).
//
// This class's own output is *always* ambisonic-shaped (config.
// numberOfChannels() - 4 at order 1, 9 at order 2 for every real top-level
// song config, since ChannelConfiguration::STEREO doesn't exist at all -
// see ambisonic_channels_), never a plain stereo signal. SongState
// accumulates it directly into the top-level mixer, which is guaranteed to
// be ambisonic-shaped too - this class itself has no notion of "stereo
// bus" at all.
//
// Slot B is processed first each block, then its pre-encode tap sum
// (BusEffect::getChainSendSum()), scaled by its own chain-send ratio
// (BusEffect::getChainSendLevel()), is added into slot A's input before
// slot A processes - a same-block (not one-block-delayed) chain send.
// Slot A is always the chain's terminal: its own chain-send ratio exists
// (every BusEffect has one, uniformly) but is never read by anything,
// since nothing sits after it. Every slot's taps are then encoded into
// the ambisonic bus uniformly via each effect's own getTapDirection()/
// getWetLevel()/getTapEncoder() - there is no special-cased "slot A's
// directions are fixed, computed once" fast path any more (see
// FDNReverb::getTapDirection(), which now owns what this class used to
// hardcode for whichever effect happened to be in slot A).
class SendBusProcessor {
 public:
  explicit SendBusProcessor(const ChannelConfiguration & config);

  static constexpr int kSlotA = 0;
  static constexpr int kSlotB = 1;

  // Installs a fully-constructed effect into a slot, taking ownership -
  // called once per slot at song load (SongState::initialize()) and never
  // again for the lifetime of a loaded song. The caller (SongState) is
  // responsible for having already called the new effect's
  // loadParameters()/setRowDuration() as needed before installing it here;
  // this method just takes ownership and nothing else.
  void setSlotEffect(int slot, std::unique_ptr<BusEffect> effect);

  BusEffect & getSlotEffect(int slot) { return *slots_[static_cast<size_t>(slot)]; }
  const BusEffect & getSlotEffect(int slot) const { return *slots_[static_cast<size_t>(slot)]; }

  // aux_a_mono/aux_b_mono: single-channel (mono) cross-track sums for
  // this block, feeding slot A/B's input respectively (before any chain
  // send - see the class comment above). Always processes, even when both
  // are silent, so every slot's internal tail/feedback/pattern state
  // stays continuous across blocks (same reasoning as
  // AmbisonicBinauralMixer's overlap-add tail).
  void process(const SampleData & aux_a_mono, const SampleData & aux_b_mono, int frames);

  const SampleData & getBusAmbisonic() const { return bus_ambisonic_; }

 private:
  // Both default-constructed to NullBusEffect (BusEffectRegistry.h) in
  // this class's own constructor, so process() is always safe to call
  // even before SongState::initialize() ever calls setSlotEffect() (e.g.
  // a test constructing this class directly) - never a raw nullptr.
  std::array<std::unique_ptr<BusEffect>, 2> slots_;

  // Scratch buffers reused across process() calls - resize() only grows
  // the underlying allocation the first time a given (or larger) block
  // size is seen, so this doesn't allocate on the audio thread once
  // warmed up.
  std::vector<float> chain_scratch_;     // slot B's getChainSendSum() output
  std::vector<float> combined_a_input_;  // aux_a_mono + chain_scratch_ * chain level

  // config.numberOfChannels() - 4 or 9 for every real top-level AMBISONIC
  // config. The only other value ever seen here is 1, for the synthetic
  // top-level MONO config one regression test constructs directly -
  // SongState only ever calls process()/getBusAmbisonic() when its own
  // config is AMBISONIC, so that case never actually reaches this class's
  // process() method in practice. Fixed for this instance's lifetime.
  int ambisonic_channels_;

  SampleData bus_ambisonic_;    // always ambisonic_channels_ channels
};

#endif
