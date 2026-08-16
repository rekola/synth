#include "TestFramework.h"

#include "../src/effects/EnvelopeFilter.h"
#include "../src/MemoryParameterSource.h"
#include "../src/ChannelConfiguration.h"

// EnvelopeFilterVoiceState is file-local to effects/EnvelopeFilter.cpp (no
// header declaration) - constructed here the same way SF2ModulatorTests.cpp
// reaches SoundFontVoice, through the public Track/VoiceState API only:
// build an EnvelopeFilter, load its envelope parameters, createVoiceState()
// (the voice-role factory - see plans/trackstate-voicestate-split.md; the
// note-lifecycle behavior these tests exercise - stopNote()/fastRelease()/
// isActive() after render() - only exists on that role, not
// EnvelopeFilterTrackState's). With attack=hold=decay=0 the envelope is
// already in SUSTAIN, at the configured sustain level, the instant
// createVoiceState() returns - no separate playNote() trigger needed
// (EnvelopeState's own constructor already walks NONE->...->SUSTAIN based
// on the envelope's own timing).
//
// See plans/silence-kill-threshold.md - these mirror the SF2 side's
// tests in tests/SF2ModulatorTests.cpp for the same threshold, applied to
// EnvelopeFilterVoiceState's own (simpler - no separate static gain term,
// no modenv_) envelope instead of SoundFontVoice's ampenv_.

namespace {

  std::unique_ptr<VoiceState> makeEnvelopeFilterState(const ChannelConfiguration & config, float sustain, float release) {
    EnvelopeFilter filter;
    MemoryParameterSource params;
    params.set("attack", 0.0f);
    params.set("hold", 0.0f);
    params.set("decay", 0.0f);
    params.set("sustain", sustain);
    params.set("release", release);
    filter.loadParameters(params);
    return filter.createVoiceState(config);
  }

}

TEST(envelope_filter_state_stays_active_while_held_even_below_the_silence_floor) {
  ChannelConfiguration config(44100);
  // Sustain well below the -60dB default floor (0.0001 ~= -80dB), long
  // release - if the silence-kill threshold ever fired outside RELEASE,
  // this held (never stopNote()'d) voice would be killed immediately.
  auto state = makeEnvelopeFilterState(config, /*sustain=*/0.0001f, /*release=*/5.0f);
  CHECK(state->isActive());

  for (int i = 0; i < 8; i++) {
    state->render(4096);
    CHECK(state->isActive()); // still held - must never be killed regardless of how quiet
  }
}

TEST(envelope_filter_state_releasing_below_the_floor_is_freed_early) {
  ChannelConfiguration config(44100);
  // Sustain already below the floor, long (5s) release - stopNote() only
  // starts the release *timer*, the level itself doesn't jump, so without
  // the threshold this would keep "releasing" (inaudibly) for the full 5s.
  auto state = makeEnvelopeFilterState(config, /*sustain=*/0.0001f, /*release=*/5.0f);
  state->stopNote();

  bool became_inactive = false;
  for (int i = 0; i < 8 && !became_inactive; i++) {
    state->render(4096); // 8*4096 ~= 743ms, comfortably short of the 5s authored release
    if (!state->isActive()) became_inactive = true;
  }
  CHECK(became_inactive);
}

TEST(envelope_filter_state_releasing_above_the_floor_is_not_freed_early) {
  ChannelConfiguration config(44100);
  // Full sustain (well above the floor), long release - shortly after
  // stopNote() the level has barely begun to decay, so this must still
  // be genuinely active (naturally releasing), not already finished.
  auto state = makeEnvelopeFilterState(config, /*sustain=*/1.0f, /*release=*/5.0f);
  state->stopNote();

  state->render(4096); // ~93ms - far too early in a 5s release to cross -60dB
  CHECK(state->isActive());
}

// Regression test for the missing fastRelease() override - without it,
// fastRelease() fell through to VoiceState's generic default (recurse into
// children, no envelope involvement at all), so a full 1.0s authored
// release would never even start: this voice would stay reported "active"
// (envelope still sitting in SUSTAIN) for the entire budget below, in
// addition to hard-cutting whatever child it wrapped with no ramp. See
// EnvelopeFilterVoiceState::fastRelease()'s own comment.
TEST(envelope_filter_state_fast_release_finishes_much_sooner_than_the_authored_release) {
  ChannelConfiguration config(44100);
  // Full sustain, a 1.0s authored release - fastRelease() must reclaim
  // this via the short (~10ms TSF_FASTRELEASETIME) compressed release, not
  // the full authored one.
  auto state = makeEnvelopeFilterState(config, /*sustain=*/1.0f, /*release=*/1.0f);
  state->fastRelease();

  bool became_inactive = false;
  for (int i = 0; i < 8 && !became_inactive; i++) {
    state->render(4096); // 8*4096 ~= 743ms, comfortably short of the 1.0s authored release
    if (!state->isActive()) became_inactive = true;
  }
  CHECK(became_inactive);
}
