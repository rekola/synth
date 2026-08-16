#include "TestFramework.h"

#include "../src/Song.h"
#include "../src/InstrumentTrack.h"
#include "../src/SongState.h"
#include "../src/InstrumentTrackState.h"
#include "../src/OscillatorVoice.h"
#include "../src/Oscillator.h"
#include "../src/WaveformType.h"
#include "../src/RenderContext.h"
#include "../src/SphericalPosition.h"
#include "../src/MixerFactory.h"
#include "../src/MixerType.h"
#include "../src/Mixer.h"
#include "../src/ChannelConfiguration.h"
#include "../src/constants.h"

using namespace std;

// 2Lxx/2Rxx - see docs/commands.md and Command.h's own comments.
TEST(azimuth_slide_command_parses_direction_and_magnitude) {
  Command left("2L10");
  CHECK(left.isAzimuthSlide());
  CHECK_NEAR(left.getAzimuthSlidePerTick(), -16.0f, 0.001f); // 0x10 = 16

  Command right("2R0A");
  CHECK(right.isAzimuthSlide());
  CHECK_NEAR(right.getAzimuthSlidePerTick(), 10.0f, 0.001f); // 0x0A = 10

  Command unrelated("ZB02");
  CHECK(!unrelated.isAzimuthSlide());
}

// InstrumentVoice::adjustAzimuth() (the leaf override VoiceState::
// adjustAzimuth()'s default child-recursion ultimately reaches) - unlike
// every other positional field, this changes live, mid-note.
TEST(instrument_voice_adjust_azimuth_moves_a_live_voice) {
  ChannelConfiguration config(44100);
  OscillatorVoice voice(config, SphericalPosition{ 10.0f, 0, 0 }, 1.0f, WaveformType::SINE, 1.0f, 0.5f);
  voice.playNote(440.0f, 0.6f, 42);

  CHECK_NEAR(voice.getPosition().azimuth, 10.0f, 0.001f);
  voice.adjustAzimuth(15.0f);
  CHECK_NEAR(voice.getPosition().azimuth, 25.0f, 0.001f);
  voice.adjustAzimuth(-40.0f);
  CHECK_NEAR(voice.getPosition().azimuth, -15.0f, 0.001f);
}

// RenderContext's own azimuth-tick timeline (separate from the note
// pending_events_ one) - same-frame ticks accumulate rather than
// replacing each other, and updateFrameOffset() carries unconsumed ticks
// forward exactly like it already does for note events.
TEST(render_context_accumulates_and_carries_azimuth_ticks) {
  RenderContext context(ChannelConfiguration(44100));

  context.addPendingAzimuthTick(/*track_id*/ 1, /*frame*/ 100, 5.0f);
  context.addPendingAzimuthTick(1, 100, 3.0f); // same frame - should sum, not overwrite
  context.addPendingAzimuthTick(1, 300, 2.0f); // past this block - carried forward below

  auto & ticks = context.getPendingAzimuthTicks(1);
  CHECK(ticks.size() == 2);
  CHECK_NEAR(ticks[100], 8.0f, 0.001f);
  CHECK_NEAR(ticks[300], 2.0f, 0.001f);

  context.updateFrameOffset(-200); // as if a 200-frame block had just been consumed
  auto & shifted = context.getPendingAzimuthTicks(1);
  CHECK(shifted.size() == 1); // frame 100 - 200 < 0, dropped, same as a note event would be
  CHECK_NEAR(shifted[100], 2.0f, 0.001f);
}

// Full pipeline: a held note across a row carrying 2Rxx slides the
// track's own live azimuth by constants::TICKS_PER_ROW * the command's
// per-tick amount over the course of that one row (InstrumentTrackState::
// adjustAzimuth() also nudges every currently-sounding voice by the same
// amount, in lockstep with the track - see instrument_voice_adjust_azimuth_
// moves_a_live_voice above for that piece in isolation).
TEST(azimuth_slide_moves_the_track_over_the_row) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE)); // instrument_id 0
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  auto & scene0 = song.addScene();
  scene0.setNote(0, track.getInternalId(), 0, Note(60, 100));
  scene0.setCommand(0, track.getInternalId(), Command("2R05")); // +5 deg/tick, right

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(row_samples, song, *mixer);

  auto * track_state = dynamic_cast<InstrumentTrackState *>(state.getChildByInternalId(track.getInternalId()));
  CHECK(track_state != nullptr);
  CHECK_NEAR(track_state->getAzimuth(), constants::TICKS_PER_ROW * 5.0f, 0.01f);
}
