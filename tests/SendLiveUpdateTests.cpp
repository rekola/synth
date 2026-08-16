#include "TestFramework.h"

#include "../Song.h"
#include "../InstrumentTrack.h"
#include "../SongState.h"
#include "../InstrumentTrackState.h"
#include "../OscillatorVoice.h"
#include "../Oscillator.h"
#include "../WaveformType.h"
#include "../SphericalPosition.h"
#include "../MixerFactory.h"
#include "../MixerType.h"
#include "../Mixer.h"
#include "../ChannelConfiguration.h"
#include "../AudioBuffer.h"

#include <algorithm>
#include <cmath>

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

  auto & scene0 = song.addScene();
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
