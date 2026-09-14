#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/state/SongState.h"
#include "../src/state/InstrumentTrackState.h"
#include "../src/state/TrackInfo.h"
#include "../src/instruments/OscillatorVoice.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/ambisonic/SphericalPosition.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/audio/AudioBuffer.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

using namespace std;

namespace {
  float maxAbs(const float * data, int frames) {
    float m = 0.0f;
    for (int i = 0; i < frames; i++) m = std::max(m, std::fabs(data[i]));
    return m;
  }
}

// InstrumentVoice::adjustSendA()/adjustSendB()/adjustSendMain() (the leaf
// overrides VoiceState::adjustSendA()/adjustSendB()/adjustSendMain()'s
// default child-recursion ultimately reaches) - like adjustAzimuth(), these
// change live, mid-note: encodePosition() (InstrumentVoice.h) reads
// getSends() fresh every render() call, deciding AuxA/AuxB/Main channel
// presence from the current values, not whatever was baked in at
// construction.
TEST(instrument_voice_adjust_send_a_reaches_an_already_active_voice) {
  ChannelConfiguration config(44100);
  OscillatorVoice voice(config, SphericalPosition{ 0, 0, 1.0f }, 1.0f, WaveformType::SINE, 1.0f, 0.5f);
  voice.playNote(440.0f, 0.6f, 42);

  auto silent = voice.render(64);
  CHECK(!silent.hasChannel(Channel::AuxA)); // Send A still at its 0.0 default

  voice.adjustSendA(0.8f);
  auto loud = voice.render(64);
  CHECK(loud.hasChannel(Channel::AuxA));
  CHECK(maxAbs(loud.getChannel(Channel::AuxA), 64) > 1e-4f);
}

TEST(instrument_voice_adjust_send_b_reaches_an_already_active_voice) {
  ChannelConfiguration config(44100);
  OscillatorVoice voice(config, SphericalPosition{ 0, 0, 1.0f }, 1.0f, WaveformType::SINE, 1.0f, 0.5f);
  voice.playNote(440.0f, 0.6f, 42);

  auto silent = voice.render(64);
  CHECK(!silent.hasChannel(Channel::AuxB));

  voice.adjustSendB(0.8f);
  auto loud = voice.render(64);
  CHECK(loud.hasChannel(Channel::AuxB));
  CHECK(maxAbs(loud.getChannel(Channel::AuxB), 64) > 1e-4f);
}

TEST(instrument_voice_adjust_send_main_reaches_an_already_active_voice) {
  ChannelConfiguration config(44100);
  // SendLevels{} defaults to main=1.0 - starts audible on Main.
  OscillatorVoice voice(config, SphericalPosition{ 0, 0, 1.0f }, 1.0f, WaveformType::SINE, 1.0f, 0.5f);
  voice.playNote(440.0f, 0.6f, 42);

  auto loud = voice.render(64);
  CHECK(loud.hasChannel(Channel::Main));

  voice.adjustSendMain(0.0f); // silence Main entirely, mid-note
  auto silent = voice.render(64);
  CHECK(!silent.hasChannel(Channel::Main));
}

// Full pipeline: InstrumentTrackState::setSendA() (the Launchpad/UI Send A
// knob's actual entry point, via Player.cpp's SET_TRACK_SEND_A handling)
// reaches a voice already sounding when the knob turns, not just whatever
// note plays next - proven here by checking the raw per-block AuxA sum
// SongState exposes (getAuxASum(), the same one the UI's volume meter
// reads) before and after the live update, both while the same note is
// still held throughout.
TEST(track_state_set_send_a_reaches_an_already_active_voice) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE)); // instrument_id 0
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));
  // Send A defaults to 0 on the track/model - setSendA() below is the live
  // knob path, independent of whatever a note's own pattern data carries.

  auto & scene0 = song.addSection();
  scene0.setNote(0, track.getInternalId(), 0, Note(60, 100));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  int row_samples = config.getSampleInterval(song.getTempo());
  int quarter = row_samples / 4;

  // Trigger the note and render a quarter-row with Send A still at 0.
  state.renderBlock(quarter, song, *mixer);
  CHECK(maxAbs(state.getAuxASum().getChannelData(0), quarter) < 1e-6f);

  auto * track_state = dynamic_cast<InstrumentTrackState *>(state.getChildByInternalId(track.getInternalId()));
  CHECK(track_state != nullptr);
  track_state->setSendA(0.8f); // the live knob - the note above is still sounding

  // Render another quarter-row (still the same held note, no new note-on)
  // and confirm AuxA is now audibly non-zero.
  state.renderBlock(quarter, song, *mixer);
  CHECK(maxAbs(state.getAuxASum().getChannelData(0), quarter) > 1e-4f);
}

// LeafTrackState::glideSendA()/advanceSendRamps() - the server-side
// counterpart of a Launchpad fader press, now instant/one-shot on the
// Launchpad side. Proves the glide actually spreads across multiple
// render blocks (a real interpolation, not an instant jump on the first
// one) by reading it back through TrackInfo::getLiveSendA() - the exact
// channel Controller::receivePlaybackSnapshot() uses to keep the model
// honest, not a private field this test reaches into directly. Checked
// in dB, not linear gain - the ramp itself interpolates in dB (the same
// space the Launchpad row layout is defined in), so "halfway through the
// glide's own duration" means halfway in dB, not halfway in linear gain
// (which would be a tiny fraction of the way there in dB terms - see
// LeafTrackState.h's own comment on why ramping linear directly looked
// wrong: fast near the bottom, crawling near the top).
TEST(leaf_track_state_glide_send_a_interpolates_across_render_blocks) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  // Per-track state nodes are built lazily, the first time renderBlock()
  // actually processes this track (see the render loop's own
  // getChildByInternalId() calls) - not by initialize() itself, so a
  // warm-up block is needed before there's any LeafTrackState to resolve
  // at all.
  state.renderBlock(1, song, *mixer);
  auto * track_state = dynamic_cast<InstrumentTrackState *>(state.getChildByInternalId(track.getInternalId()));
  CHECK(track_state != nullptr);
  if (!track_state) return;

  // An instant set to a known, non-extreme starting dB value first - the
  // track's own true default (Send A off, the -100dB floor) makes a poor
  // "halfway" example, since a floor value isn't a real measured level.
  track_state->setSendA(powf(10.0f, -40.0f * 0.05f)); // -40dB, as linear gain

  int total_frames = 1000;
  track_state->glideSendA(0.0f, total_frames); // -40dB -> 0dB

  std::unordered_map<int, TrackInfo> info;

  // Partway through the glide: real progress, not yet at the target -
  // roughly -20dB (halfway between -40 and 0), not roughly -40dB (as a
  // linear-space ramp would still show at this point) or roughly 0dB (as
  // an instant jump would).
  state.renderBlock(total_frames / 2, song, *mixer);
  state.getAllTrackInfo(info);
  float halfway_db = 20.0f * log10f(info[track.getInternalId()].getLiveSendA());
  CHECK_NEAR(halfway_db, -20.0f, 1.0f);

  // The rest of the glide: settled exactly at the target, not overshot.
  state.renderBlock(total_frames / 2, song, *mixer);
  state.getAllTrackInfo(info);
  CHECK_NEAR(info[track.getInternalId()].getLiveSendA(), 1.0f, 1e-3f); // 0dB = unity
}

// An instant setSendA() (the plain live-knob path, unrelated to a glide)
// must win outright over a glide still in flight - ValueRamp::snapTo()'s
// own guarantee, proven here through the actual LeafTrackState call site
// rather than just the ramp in isolation.
TEST(leaf_track_state_instant_set_send_a_cancels_an_in_flight_glide) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  state.renderBlock(1, song, *mixer); // see the sibling test's own comment on why this warm-up is needed
  auto * track_state = dynamic_cast<InstrumentTrackState *>(state.getChildByInternalId(track.getInternalId()));
  CHECK(track_state != nullptr);
  if (!track_state) return;

  track_state->glideSendA(0.0f, 1000); // toward 0dB/unity
  state.renderBlock(500, song, *mixer); // now partway through the glide

  track_state->setSendA(0.3f); // an instant set (linear gain), mid-glide

  std::unordered_map<int, TrackInfo> info;
  state.renderBlock(500, song, *mixer); // the rest of what would have been the glide
  state.getAllTrackInfo(info);
  // Stayed at the instant-set value - the glide it interrupted never
  // resumed and dragged it back toward unity.
  CHECK_NEAR(info[track.getInternalId()].getLiveSendA(), 0.3f, 1e-4f);
}

// LeafTrackState::glideAzimuth()/advanceAzimuthRamp() - Pan's own
// equivalent of glideSendA()/advanceSendRamps() above. Unlike Send
// (interpolated in dB), azimuth ramps directly in degrees - no equivalent
// "uneven row spacing" reason to warp it - so the halfway check here is
// plain linear interpolation, not the dB-space one the Send sibling test
// uses.
TEST(leaf_track_state_glide_azimuth_interpolates_across_render_blocks) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  state.renderBlock(1, song, *mixer); // see the sibling Send A test's own comment on why this warm-up is needed
  auto * track_state = dynamic_cast<InstrumentTrackState *>(state.getChildByInternalId(track.getInternalId()));
  CHECK(track_state != nullptr);
  if (!track_state) return;

  track_state->setAzimuth(-40.0f); // a known, non-extreme starting point
  int total_frames = 1000;
  track_state->glideAzimuth(0.0f, total_frames); // -40 -> 0 degrees, the short way (40 degrees, well under the +-180 wrap)

  std::unordered_map<int, TrackInfo> info;
  state.renderBlock(total_frames / 2, song, *mixer);
  state.getAllTrackInfo(info);
  CHECK_NEAR(info[track.getInternalId()].getLiveAzimuth(), -20.0f, 1.0f); // halfway between -40 and 0

  state.renderBlock(total_frames / 2, song, *mixer);
  state.getAllTrackInfo(info);
  CHECK_NEAR(info[track.getInternalId()].getLiveAzimuth(), 0.0f, 1e-3f);
}

// glideAzimuth()'s own circular shortest-path direction (its own doc
// comment): a glide from 170 to -170 degrees is really only a 20-degree
// move through +-180, not a 340-degree one back through 0 - proven by
// checking the glide's own midpoint lands near +-180 (the short way),
// never anywhere near 0 (what the long way around would pass through).
TEST(leaf_track_state_glide_azimuth_picks_the_shorter_way_around_the_circle) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  state.renderBlock(1, song, *mixer);
  auto * track_state = dynamic_cast<InstrumentTrackState *>(state.getChildByInternalId(track.getInternalId()));
  CHECK(track_state != nullptr);
  if (!track_state) return;

  track_state->setAzimuth(170.0f);
  int total_frames = 1000;
  track_state->glideAzimuth(-170.0f, total_frames);

  std::unordered_map<int, TrackInfo> info;
  state.renderBlock(total_frames / 2, song, *mixer);
  state.getAllTrackInfo(info);
  // The short way's own midpoint is +-180 (170 + half of the 20-degree
  // wrapped delta) - nowhere near 0, which the long way around would
  // have passed through at this same point.
  CHECK(std::fabs(info[track.getInternalId()].getLiveAzimuth()) > 175.0f);

  state.renderBlock(total_frames / 2, song, *mixer);
  state.getAllTrackInfo(info);
  // Settled at a value congruent to -170 degrees mod 360 (190, since
  // position_.azimuth is left free to end up outside +-180 - see
  // glideAzimuth()'s own comment), not back at the starting +170.
  CHECK_NEAR(info[track.getInternalId()].getLiveAzimuth(), 190.0f, 1e-2f);
}

// Regression test for a real reported bug: repeatedly pressing two Pan
// rows exactly 180 degrees apart (a perfectly ordinary "back and forth"
// gesture) walked position_.azimuth up by 180 degrees on *every single*
// press - the exact-180-degrees tie always broke the same way, so it
// never averaged out, only accumulated (after enough presses, the live
// azimuth ran into the thousands of degrees). glideAzimuth()'s own
// rebase-at-rest (its own top comment) fixes this: each press still
// settles 180 degrees from the last, but rebasing back into range
// whenever the ramp is at rest keeps the *reference point* itself
// bounded, so the sequence cycles between two values instead of
// climbing forever.
TEST(leaf_track_state_glide_azimuth_does_not_drift_under_repeated_opposite_presses) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  state.renderBlock(1, song, *mixer);
  auto * track_state = dynamic_cast<InstrumentTrackState *>(state.getChildByInternalId(track.getInternalId()));
  CHECK(track_state != nullptr);
  if (!track_state) return;

  int total_frames = 100;
  for (int i = 0; i < 40; i++) {
    float target = (i % 2 == 0) ? -180.0f : 0.0f; // two rows exactly 180 degrees apart
    track_state->glideAzimuth(target, total_frames);
    state.renderBlock(total_frames, song, *mixer); // let each press fully settle before the next
  }

  std::unordered_map<int, TrackInfo> info;
  state.getAllTrackInfo(info);
  // Stayed within a couple of turns of the two targets - nowhere near the
  // ~7200 degrees 40 unrebased +-180 steps would have accumulated before
  // this fix.
  CHECK(std::fabs(info[track.getInternalId()].getLiveAzimuth()) < 720.0f);
}

// An instant setAzimuth() must win outright over a glide still in
// flight, same guarantee leaf_track_state_instant_set_send_a_cancels_an_
// in_flight_glide proves for Send A.
TEST(leaf_track_state_instant_set_azimuth_cancels_an_in_flight_glide) {
  Song song;
  song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
  auto & track = song.addTrack(make_unique<InstrumentTrack>(0));

  ChannelConfiguration config(44100, 1);
  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);
  state.setIsPlaying(true);

  state.renderBlock(1, song, *mixer);
  auto * track_state = dynamic_cast<InstrumentTrackState *>(state.getChildByInternalId(track.getInternalId()));
  CHECK(track_state != nullptr);
  if (!track_state) return;

  track_state->glideAzimuth(90.0f, 1000);
  state.renderBlock(500, song, *mixer); // now partway through the glide

  track_state->setAzimuth(30.0f); // an instant set, mid-glide

  std::unordered_map<int, TrackInfo> info;
  state.renderBlock(500, song, *mixer); // the rest of what would have been the glide
  state.getAllTrackInfo(info);
  CHECK_NEAR(info[track.getInternalId()].getLiveAzimuth(), 30.0f, 1e-3f);
}
