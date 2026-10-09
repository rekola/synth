#include "SendBusProcessor.h"
#include "BusEffectRegistry.h"

#include <algorithm>

using namespace std;

SendBusProcessor::SendBusProcessor(const ChannelConfiguration & config)
    : ambisonic_channels_(config.numberOfChannels()),
      hold_frames_(static_cast<int>(kBusAudibleHoldSeconds * static_cast<float>(config.getAudioOutSampleRate()))) {
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
SendBusProcessor::process(const AudioBuffer & aux_a_mono, const AudioBuffer & aux_b_mono, int frames, float return_a, float return_b) {
  auto & slot_a = *slots_[kSlotA];
  auto & slot_b = *slots_[kSlotB];

  // Slot B runs first so its chain send can reach slot A within the same
  // block (a deliberate zero-added-latency choice, not the alternative of
  // chaining the previous block's tail forward - see the plan this
  // implements).
  slot_b.process(aux_b_mono.getChannelData(0), frames);

  if (static_cast<int>(chain_scratch_.size()) != frames) chain_scratch_.resize(static_cast<size_t>(frames));
  if (static_cast<int>(combined_a_input_.size()) != frames) combined_a_input_.resize(static_cast<size_t>(frames));

  slot_b.getChainSendSum(chain_scratch_.data(), frames);
  float chain_level = slot_b.getChainSendLevel();
  auto a_in = aux_a_mono.getChannelData(0);
  for (int i = 0; i < frames; i++) {
    combined_a_input_[static_cast<size_t>(i)] = a_in[i] + chain_scratch_[static_cast<size_t>(i)] * chain_level;
  }

  slot_a.process(combined_a_input_.data(), frames);

  if (bus_ambisonic_.numberOfFrames() != frames || bus_ambisonic_.numberOfChannels() != ambisonic_channels_) {
    bus_ambisonic_ = AudioBuffer(static_cast<short>(ambisonic_channels_), frames);
  }
  bus_ambisonic_.zero();

  // Uniform for both slots, regardless of which concrete effect (or
  // NullBusEffect, 0 taps - a trivial no-op iteration range) occupies
  // them - no per-type direction/gain special case any more (see this
  // class's own header comment for why the old "slot A's directions are
  // fixed, computed once at construction" fast path was deliberately
  // dropped).
  for (auto * slot : { &slot_a, &slot_b }) {
    int n = slot->getNumTaps();
    float wet = slot->getWetLevel() * (slot == &slot_a ? return_a : return_b);
    for (int t = 0; t < n; t++) {
      auto gains = computeAmbisonicGains(slot->getTapDirection(t));
      for (auto & g : gains) g *= wet;
      slot->getTapEncoder(t).encodeBlock(bus_ambisonic_, slot->getTap(t), frames, gains);
    }
  }

  // Direct-channel path (BusEffect.h) - a second, parallel output kind
  // alongside the point-source taps above, for effects whose output can't
  // be expressed as one mono signal gain-panned to a single direction
  // (see plans/drum-bus-saturator.md). Uniform for both slots, same "no
  // per-type branching" convention as the tap loop above; a no-op for
  // every effect that doesn't override it.
  for (auto * slot : { &slot_a, &slot_b }) {
    float slot_return = slot == &slot_a ? return_a : return_b;
    if (slot_return == 1.0f) {
      slot->encodeDirect(bus_ambisonic_, frames);
      continue;
    }
    if (direct_scratch_.numberOfFrames() != frames || direct_scratch_.numberOfChannels() != ambisonic_channels_) {
      direct_scratch_ = AudioBuffer(static_cast<short>(ambisonic_channels_), frames);
    }
    direct_scratch_.zero();
    slot->encodeDirect(direct_scratch_, frames);
    for (int c = 0; c < ambisonic_channels_; c++) {
      auto src = direct_scratch_.getChannelData(c);
      auto dst = bus_ambisonic_.getChannelData(c);
      for (int i = 0; i < frames; i++) dst[i] += src[i] * slot_return;
    }
  }

  float peak = 0.0f;
  for (int c = 0; c < ambisonic_channels_; c++) {
    peak = std::max(peak, dsp::maxAbs(bus_ambisonic_.getChannelData(c), frames));
  }
  hold_remaining_ = peak >= kBusAudibleFloor ? hold_frames_ : std::max(0, hold_remaining_ - frames);
}
