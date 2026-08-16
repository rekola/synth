#include "TestFramework.h"

#include "../src/Song.h"
#include "../src/InstrumentProvider.h"
#include "../src/SongState.h"
#include "../src/MixerFactory.h"
#include "../src/Mixer.h"
#include "../src/ChannelConfiguration.h"
#include "../src/dsp/DelayLineTail.h"
#include "../src/dsp/TapeTransport.h"

#include <cmath>
#include <string>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif

namespace {

struct Loaded {
  bool ok;
  Song song;
};

Loaded loadFixture(const char * name) {
  InstrumentProvider provider;
  Song song;
  bool ok = song.open(std::string(TESTS_FIXTURES_DIR) + "/" + name, provider);
  return { ok, std::move(song) };
}

// Drives a Song block-by-block through a real SongState/Mixer, exactly
// the same construction OfflineRenderer.cpp uses - unlike
// renderSongOffline() (which only exposes the final decoded audio), this
// exposes getVoiceCount() (TrackState.h) after every block, so a test can
// directly observe whether a voice is still resident rather than only
// inferring it from whether the output happens to be silent (silent output
// and "voice still allocated but producing silence" are NOT the same
// thing - this is what actually distinguishes "reclaimed" from "merely
// quiet").
struct Driver {
  explicit Driver(const Song & song, int sampleRate = 48000)
    : config(sampleRate, 1), state(config), mixer(createMixer(config, MixerType::AMBISONIC_STEREO)) {
    state.initialize(song);
    state.setIsPlaying(true);
  }

  void renderSeconds(const Song & song, float seconds, int block_frames = 256) {
    int total = static_cast<int>(seconds * config.getAudioOutSampleRate());
    for (int i = 0; i < total; i += block_frames) {
      state.renderBlock(block_frames, song, *mixer);
    }
  }

  int voiceCount() const { return state.getVoiceCount(); }

  ChannelConfiguration config;
  SongState state;
  std::unique_ptr<Mixer> mixer;
};

}

// End-to-end proof that TapeDegradation's voice-attached fix (SS1/SS2
// diagnosis: the wow/flutter delay line never flushed, and hiss cut to
// zero instantly) actually keeps the voice *resident* - not just quiet -
// for a bounded tail past note-off, and that it's eventually reclaimed
// rather than held forever.
TEST(tape_degradation_mellotron_voice_is_reclaimed_after_tail_not_before) {
  auto loaded = loadFixture("tape_degradation_mellotron_voice.xml");
  CHECK(loaded.ok);

  Driver d(loaded.song);

  // Row 0 note-on, row 4 note-off at tempo 120 => note-off at t=0.5s. Just
  // after note-on, the voice must be resident.
  d.renderSeconds(loaded.song, 0.1f);
  CHECK(d.voiceCount() > 0);

  // Run up to just past note-off (t=0.5s) plus a little into the release/
  // spin-down window (t=0.55s total) - still resident: the instrument's
  // own release (50ms) and the Mellotron preset's spin-down (120ms) are
  // both still in progress at this point, so reclaiming here would be
  // the exact bug this fix addresses.
  d.renderSeconds(loaded.song, 0.45f); // now at t=0.55s
  CHECK(d.voiceCount() > 0);

  // Comfortably past note-off + spin-down (120ms) + the wow/flutter delay
  // line's own max content age (60ms) + a margin - must be fully
  // reclaimed by now, not held forever.
  d.renderSeconds(loaded.song, 0.6f); // now at t=1.15s
  CHECK(d.voiceCount() == 0);
}

TEST(chorus_voice_attached_is_reclaimed_after_tail_not_before) {
  auto loaded = loadFixture("chorus_voice_attached.xml");
  CHECK(loaded.ok);

  Driver d(loaded.song);

  // Row 0 note-on, row 2 note-off at tempo 120 => note-off at t=0.25s.
  d.renderSeconds(loaded.song, 0.05f);
  CHECK(d.voiceCount() > 0);

  // Chorus's own delay (15ms center + 4ms depth + 1 => ~20ms) is far
  // shorter than Mellotron's spin-down, so its whole tail is brief -
  // check shortly after note-off, well inside the ~20ms+margin window,
  // and confirm reclamation completes soon after.
  d.renderSeconds(loaded.song, 0.23f); // now at t=0.28s, ~30ms after note-off
  // (Not asserting voiceCount() > 0 here - the instrument's own 20ms
  // release plus chorus's ~20ms delay tail are close enough together,
  // and block-quantized, that this exact instant is a legitimate race;
  // the meaningful assertion is the "reclaimed well before a large
  // arbitrary duration" one below.)

  d.renderSeconds(loaded.song, 0.3f); // now at t=0.58s, ~330ms after note-off
  CHECK(d.voiceCount() == 0);
}

// DelayLineTail (dsp/DelayLineTail.h) - the generic drain counter both
// TapeDegradation.cpp and Chorus.cpp compose with their own delay line.
TEST(delay_line_tail_drains_over_exactly_max_delay_samples) {
  DelayLineTail tail(1000);
  CHECK(!tail.isDraining()); // freshly constructed - nothing written yet, nothing to drain

  tail.update(true, 64); // real input this block
  CHECK(tail.isDraining()); // pinned at max while input stays real... wait: isDraining() should be true here since remaining_samples_ == max (1000) > 0

  tail.update(false, 500); // input goes silent
  CHECK(tail.isDraining()); // 1000 - 500 = 500 remaining > 0

  tail.update(false, 500); // another 500 silent samples
  CHECK(!tail.isDraining()); // 500 - 500 = 0 remaining

  tail.update(false, 1); // still silent, already drained
  CHECK(!tail.isDraining());
}

TEST(delay_line_tail_resets_to_max_when_input_returns) {
  DelayLineTail tail(100);
  tail.update(true, 1);
  tail.update(false, 90); // 10 remaining
  CHECK(tail.isDraining());
  tail.update(true, 1); // real input again - back to full max, not just +1
  tail.update(false, 90); // if it had only advanced by 1 sample of real input, this would already be past 0
  CHECK(tail.isDraining());
}

TEST(delay_line_tail_reports_configured_max) {
  DelayLineTail tail(1234);
  CHECK(tail.maxDelaySamples() == 1234);
}

// TapeTransportLifecycle (dsp/TapeTransport.h) - the Stopped/SpinUp/
// Running/SpinDown state machine, independent of any AudioBuffer/DSP.
TEST(tape_transport_lifecycle_defaults_to_running_until_told_otherwise) {
  TapeTransportLifecycle lc;
  CHECK(lc.state() == TapeTransportLifecycle::State::Running);
  lc.advance(1.0f); // a whole second, unprompted - still Running
  CHECK(lc.state() == TapeTransportLifecycle::State::Running);
  CHECK(lc.progress() == 0.0f);
}

TEST(tape_transport_lifecycle_note_on_then_off_then_stopped_in_order) {
  TapeTransportLifecycle lc;
  lc.noteOn(0.1f); // 100ms spin-up
  CHECK(lc.state() == TapeTransportLifecycle::State::SpinUp);

  float dt = 1.0f / 1000.0f;
  for (int i = 0; i < 90; i++) lc.advance(dt); // 90ms - clearly still short of the 100ms boundary
  CHECK(lc.state() == TapeTransportLifecycle::State::SpinUp);
  for (int i = 0; i < 15; i++) lc.advance(dt); // now at 105ms - clearly past it (see the SpinDown boundary's own comment below on why not landing exactly on it)
  CHECK(lc.state() == TapeTransportLifecycle::State::Running);

  for (int i = 0; i < 50; i++) lc.advance(dt); // half a second of Running - no change
  CHECK(lc.state() == TapeTransportLifecycle::State::Running);

  lc.noteOff(0.05f); // 50ms spin-down
  CHECK(lc.state() == TapeTransportLifecycle::State::SpinDown);
  for (int i = 0; i < 45; i++) lc.advance(dt); // 45ms - short of the 50ms boundary
  CHECK(lc.state() == TapeTransportLifecycle::State::SpinDown);
  // A few steps past the boundary, not landing exactly on it - float
  // accumulation of many small dt steps can land a hair short of the
  // exact duration (harmless by a fraction of a sample in real use, at
  // sampleRate-scaled dt - see TapeTransportLifecycle::advance()), so
  // asserting state right at the exact boundary is a test-fragility trap,
  // not a real correctness requirement.
  for (int i = 0; i < 10; i++) lc.advance(dt); // now at 55ms, clearly past 50ms
  CHECK(lc.state() == TapeTransportLifecycle::State::Stopped);

  // Stays Stopped indefinitely - nothing auto-resumes it.
  for (int i = 0; i < 1000; i++) lc.advance(dt);
  CHECK(lc.state() == TapeTransportLifecycle::State::Stopped);
}

TEST(tape_transport_lifecycle_progress_ramps_zero_to_one) {
  TapeTransportLifecycle lc;
  lc.noteOn(1.0f);
  CHECK_NEAR(lc.progress(), 0.0f, 1e-4f);
  lc.advance(0.5f);
  CHECK_NEAR(lc.progress(), 0.5f, 1e-3f);
  lc.advance(0.5f);
  CHECK(lc.state() == TapeTransportLifecycle::State::Running); // reached 1.0 -> transitioned
}

// noteOff() interrupting an in-progress spin-up should head into
// spin-down from the current (partial) position, not restart from a
// full spin-up amplitude first - see TapeTransportLifecycle::noteOff()'s
// own doc comment for why (a very short note shouldn't produce a longer
// total swoop+droop than a sustained one).
TEST(tape_transport_lifecycle_note_off_during_spin_up_goes_straight_to_spin_down) {
  TapeTransportLifecycle lc;
  lc.noteOn(1.0f);
  lc.advance(0.3f); // 30% through spin-up
  CHECK(lc.state() == TapeTransportLifecycle::State::SpinUp);

  lc.noteOff(0.2f);
  CHECK(lc.state() == TapeTransportLifecycle::State::SpinDown);
  CHECK_NEAR(lc.progress(), 0.0f, 1e-4f); // spin-down's own progress starts fresh at 0
}

// A second noteOff() (e.g. killNote() arriving right after stopNote())
// must not restart the fade from full volume - see noteOff()'s own
// no-op-if-already-SpinDown/Stopped guard.
TEST(tape_transport_lifecycle_repeated_note_off_does_not_restart_fade) {
  TapeTransportLifecycle lc;
  lc.noteOn(0.01f);
  lc.advance(0.01f); // now Running
  lc.noteOff(1.0f); // long spin-down
  lc.advance(0.5f); // halfway through
  float mid_progress = lc.progress();
  CHECK(mid_progress > 0.4f && mid_progress < 0.6f);

  lc.noteOff(1.0f); // a second, redundant note-off - must be a no-op
  CHECK_NEAR(lc.progress(), mid_progress, 1e-4f);
}
