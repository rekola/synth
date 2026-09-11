#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/model/Clip.h"
#include "../src/model/ArrangementOps.h"
#include "../src/state/SongState.h"
#include "../src/state/InstrumentTrackState.h"
#include "../src/instruments/OscillatorVoice.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/state/RenderContext.h"
#include "../src/ambisonic/SphericalPosition.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/audio/AudioBuffer.h"
#include "../src/util/constants.h"

#include <cmath>

using namespace std;

// 0Hxx/0Kxx - see docs/commands.md and Command.h's own comments.
TEST(azimuth_slide_command_parses_direction_and_magnitude) {
  Command left("0H10");
  CHECK(left.isAzimuthSlide());
  CHECK_NEAR(left.getAzimuthSlidePerTick(), -16.0f, 0.001f); // 0x10 = 16

  Command right("0K0A");
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

// Full pipeline: a held note across a row carrying 0Kxx slides the
// track's own live azimuth by constants::TICKS_PER_ROW * the command's
// per-tick amount over the course of that one row (InstrumentTrackState::
// adjustAzimuth() also nudges every currently-sounding voice by the same
// amount, in lockstep with the track - see instrument_voice_adjust_azimuth_
// moves_a_live_voice above for that piece in isolation).
TEST(azimuth_slide_moves_the_track_over_the_row) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE)); // instrument_id 0
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  auto & scene0 = song.addSection();
  scene0.setNote(0, track.getInternalId(), 0, Note(60, 100));
  scene0.setCommand(0, track.getInternalId(), Command("0K05")); // +5 deg/tick, right

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

// A section-level command still fires while a real Clip instance is what's
// actually supplying that row's notes - SongState.h's own per-row loop
// reads commands from the section's own background pattern unconditionally,
// never a Clip's own leaf pattern, so track-level automation (what a live
// mixer-move recording is meant to write into - see
// plans/launchpad-novation-unification.md) is never masked out just
// because a clip happens to be playing there too.
TEST(azimuth_slide_command_fires_even_while_a_clip_supplies_the_row_notes) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE)); // instrument_id 0
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  auto track_id = track.getInternalId();

  Clip clip(track_id);
  clip.getLeafPattern().setNote(0, 0, Note(60, 100)); // the clip's own note - no command of its own
  auto clip_id = song.addClip(move(clip)).getId(); // index 0

  auto & section = song.addSection();
  placeClipInstance(song, section, track_id, 0, 0);
  CHECK(section.getInstance(track_id, 0) == clip_id);
  section.setCommand(0, track_id, Command("0K05")); // +5 deg/tick, right - section-level, not on the clip

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(row_samples, song, *mixer);

  auto * track_state = dynamic_cast<InstrumentTrackState *>(state.getChildByInternalId(track_id));
  CHECK(track_state != nullptr);
  CHECK_NEAR(track_state->getAzimuth(), constants::TICKS_PER_ROW * 5.0f, 0.01f);
}

namespace {
  // RMS of the decoded stereo right-minus-left difference signal - see
  // windowedRmsDifference in RenderTests.cpp for why this (rather than
  // comparing rms(right) vs rms(left) independently) is the right measure
  // of "how right-heavy is this block."
  float rmsDifference(const AudioBuffer & stereo) {
    auto frames = stereo.numberOfFrames();
    auto * left = stereo.getChannelData(0);
    auto * right = stereo.getChannelData(1);
    double sum = 0.0;
    for (int i = 0; i < frames; i++) sum += std::pow(static_cast<double>(right[i] - left[i]), 2);
    return frames ? static_cast<float>(std::sqrt(sum / frames)) : 0.0f;
  }
}

// Full pipeline: InstrumentTrackState::setAzimuth() (the Launchpad/UI Pan
// knob's actual entry point, via Player.cpp's SET_TRACK_AZIMUTH handling)
// audibly moves a voice already sounding, not just whatever note plays
// next - proven by checking the decoded stereo balance changes mid-note,
// the same way track_state_set_send_a_reaches_an_already_active_voice
// checks Send A's own live update via getAuxASum().
TEST(track_state_set_azimuth_reaches_an_already_active_voice) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE)); // instrument_id 0
  auto & track = static_cast<InstrumentTrack &>(song.addTrack(make_unique<InstrumentTrack>(0)));
  // Azimuth defaults to 0 (dead centre) - the live knob below is what
  // actually moves it. A real (non-zero) distance is required too -
  // computeAmbisonicGains() treats distance <= 0 as "no position ever set"
  // and ignores azimuth entirely, returning a fixed W-only gain set.
  track.setDistance(1.0f);

  auto & scene0 = song.addSection();
  scene0.setNote(0, track.getInternalId(), 0, Note(60, 100));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  int quarter = row_samples / 4;

  // Trigger the note and render a quarter-row still centered.
  state.renderBlock(quarter, song, *mixer);
  CHECK_NEAR(rmsDifference(mixer->encode()), 0.0f, 1e-4f);

  auto * track_state = dynamic_cast<InstrumentTrackState *>(state.getChildByInternalId(track.getInternalId()));
  CHECK(track_state != nullptr);
  track_state->setAzimuth(90.0f); // the live knob - the note above is still sounding, hard right

  // Render another quarter-row (still the same held note, no new note-on)
  // and confirm the decoded stereo image is now audibly right-heavy.
  state.renderBlock(quarter, song, *mixer);
  CHECK(rmsDifference(mixer->encode()) > 0.05f);
}

// 0Pxx - see docs/commands.md and Command.h's own comments. An absolute
// set, not a slide (unlike 0Hxx/0Kxx) - matches Renoise's own 0Pxx "Track
// Pan" exactly, half-circle limitation included: xx only reaches -90..+90
// degrees (the front hemisphere), never "behind".
TEST(azimuth_set_command_parses_and_decodes) {
  Command left("0P00");
  CHECK(left.isAzimuthSet());
  CHECK_NEAR(left.getAzimuthSetDegrees(), -90.0f, 0.01f);

  Command center("0P80");
  CHECK(center.isAzimuthSet());
  CHECK_NEAR(center.getAzimuthSetDegrees(), 0.0f, 1.0f); // 0x80/255 isn't exactly the midpoint - within a degree is fine

  Command right("0PFF");
  CHECK(right.isAzimuthSet());
  CHECK_NEAR(right.getAzimuthSetDegrees(), 90.0f, 0.01f);

  Command unrelated("0K05");
  CHECK(!unrelated.isAzimuthSet());
}

// Full pipeline: a 0Pxx command at a section-level row sets the track's
// own live azimuth the instant the row starts, reaching an already-
// sounding voice too - the same live-knob mechanism Controller::
// setTrackAzimuth()/etc. (a real Launchpad Pan-row press) already uses,
// proven audibly via the same rmsDifference() check
// track_state_set_azimuth_reaches_an_already_active_voice uses.
TEST(azimuth_set_command_sets_azimuth_over_the_row) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE)); // instrument_id 0
  auto & track = static_cast<InstrumentTrack &>(song.addTrack(make_unique<InstrumentTrack>(0)));
  track.setDistance(1.0f); // computeAmbisonicGains() ignores azimuth entirely at distance <= 0

  auto & section = song.addSection();
  section.setNote(0, track.getInternalId(), 0, Note(60, 100));
  section.setCommand(0, track.getInternalId(), Command("0PFF")); // hard right

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  state.renderBlock(row_samples, song, *mixer);

  CHECK(rmsDifference(mixer->encode()) > 0.05f);
}
