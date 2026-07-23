#include "SendBusProcessor.h"
#include "BusEffectRegistry.h"

using namespace std;

SendBusProcessor::SendBusProcessor(const ChannelConfiguration & config)
  : ambisonic_channels_(config.numberOfChannels()) {
  // Safe, silent defaults until SongState::initialize() installs the
  // song's real slot configuration - see slots_'s own doc comment.
  slots_[kSlotA] = make_unique<NullBusEffect>(config.getAudioOutSampleRate());
  slots_[kSlotB] = make_unique<NullBusEffect>(config.getAudioOutSampleRate());
}

void
SendBusProcessor::setSlotEffect(int slot, std::unique_ptr<BusEffect> effect) {
  slots_[static_cast<size_t>(slot)] = std::move(effect);
}

void
SendBusProcessor::process(const SampleData & send_a_mono, const SampleData & send_b_mono, int frames) {
  auto & slot_a = *slots_[kSlotA];
  auto & slot_b = *slots_[kSlotB];

  // Slot B runs first so its chain send can reach slot A within the same
  // block (a deliberate zero-added-latency choice, not the alternative of
  // chaining the previous block's tail forward - see the plan this
  // implements).
  slot_b.process(send_b_mono.getChannelData(0), frames);

  if (static_cast<int>(chain_scratch_.size()) != frames) chain_scratch_.resize(static_cast<size_t>(frames));
  if (static_cast<int>(combined_a_input_.size()) != frames) combined_a_input_.resize(static_cast<size_t>(frames));

  slot_b.getChainSendSum(chain_scratch_.data(), frames);
  float chain_level = slot_b.getChainSendLevel();
  auto a_in = send_a_mono.getChannelData(0);
  for (int i = 0; i < frames; i++) {
    combined_a_input_[static_cast<size_t>(i)] = a_in[i] + chain_scratch_[static_cast<size_t>(i)] * chain_level;
  }

  slot_a.process(combined_a_input_.data(), frames);

  if (bus_ambisonic_.numberOfFrames() != frames || bus_ambisonic_.numberOfChannels() != ambisonic_channels_) {
    bus_ambisonic_ = SampleData(static_cast<short>(ambisonic_channels_), frames);
  }
  bus_ambisonic_.zero();

  // Uniform for both slots, regardless of which concrete effect (or
  // NullBusEffect, 0 taps - a trivial no-op iteration range) occupies
  // them - no per-type direction/gain special case any more (see this
  // class's own header comment for why the old "slot A's directions are
  // fixed, computed once at construction" fast path was deliberately
  // dropped).
  bool any_taps = false;
  for (auto * slot : { &slot_a, &slot_b }) {
    int n = slot->getNumTaps();
    if (n > 0) any_taps = true;
    float wet = slot->getWetLevel();
    for (int t = 0; t < n; t++) {
      auto gains = computeAmbisonicGains(slot->getTapDirection(t));
      for (auto & g : gains) g *= wet;
      slot->getTapEncoder(t).encodeBlock(bus_ambisonic_, slot->getTap(t), frames, gains);
    }
  }

  // Only claim non-zero output when at least one slot could ever actually
  // produce any (0 taps, i.e. both slots empty/None, genuinely can't) -
  // matches the prior unconditional setNonZero() exactly for every real
  // effect (reverb/delay are always "kind of active", even decaying tails
  // from silence, the same approximation this class always made), while
  // correctly reporting silence for the empty-bus case that couldn't
  // happen before slots could be None.
  if (any_taps) bus_ambisonic_.setNonZero();
}
