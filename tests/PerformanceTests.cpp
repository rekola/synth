#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/InstrumentTrack.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/Oscillator.h"
#include "../src/instruments/WaveformType.h"
#include "../src/state/SongState.h"
#include "../src/state/InstrumentTrackState.h"
#include "../src/state/TrackInfo.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/playback/PerformanceEffect.h"
#include "../src/state/TrackStutter.h"
#include "../src/audio/AudioBuffer.h"

#include <vector>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

using namespace std;

namespace {

// A song with one held sine note, its state playing and rendered once so the
// track has a state to hold effects on.
struct SineSong {
  Song song;
  int track_id = -1;
  ChannelConfiguration config{44100, 1};
  unique_ptr<Mixer> mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state{config};

  SineSong() {
    song.addInstrument(make_unique<Oscillator>(WaveformType::SINE));
    auto track = make_unique<InstrumentTrack>(0);
    track->setDistance(2.0f); // a source on the listener has no direction to turn
    track_id = song.addTrack(std::move(track)).getInternalId();
    song.getArrangement().setNote(0, track_id, 0, Note(60, 100));
    state.initialize(song);
    state.setIsPlaying(true);
    state.renderBlock(256, song, *mixer);
  }

  // The peak of the rendered dry mix's channel `channel` over `frames`.
  float renderPeak(int frames, int channel = 0) {
    float peak = 0.0f;
    for (int done = 0; done < frames; done += 256) {
      state.renderBlock(256, song, *mixer);
      auto & bus = mixer->getRawBus();
      if (!bus.hasChannel(Channel::Main)) continue;
      auto * data = bus.getChannelData(channel);
      for (int i = 0; i < 256; i++) peak = max(peak, fabsf(data[i]));
    }
    return peak;
  }
  float auxAPeak(int frames) {
    float peak = 0.0f;
    for (int done = 0; done < frames; done += 256) {
      state.renderBlock(256, song, *mixer);
      if (!state.getAuxASum().hasChannel(Channel::Main)) continue;
      auto * data = state.getAuxASum().getChannelData(0);
      for (int i = 0; i < 256; i++) peak = max(peak, fabsf(data[i]));
    }
    return peak;
  }
  TrackInfo info() {
    unordered_map<int, TrackInfo> all;
    state.getAllTrackInfo(all);
    return all[track_id];
  }
};

} // namespace

TEST(performance_grid_rows_hold_roll_on_top_and_orbit_at_the_bottom) {
  CHECK(performance::effectForGridRow(7) == PerformanceEffect::ROLL);
  CHECK(performance::effectForGridRow(0) == PerformanceEffect::ORBIT);
  CHECK(performance::effectForGridRow(6) == PerformanceEffect::STUTTER);
  CHECK(performance::effectForGridRow(5) == PerformanceEffect::NONE);
  CHECK(performance::effectForGridRow(8) == PerformanceEffect::NONE);
}

TEST(performance_roll_rate_rises_with_pressure) {
  CHECK(performance::rollIntervalTicks(0) == 12);
  CHECK(performance::rollIntervalTicks(60) == 6);
  CHECK(performance::rollIntervalTicks(127) == 3);
}

// Duck lowers the dry signal while held and gives it back on release; the
// song's own Send Main, which the UI mirrors from the track info, stays.
TEST(performance_duck_lowers_the_dry_signal_until_released) {
  SineSong s;
  float before = s.renderPeak(4096);
  CHECK(before > 0.01f);

  s.state.setPerformance(s.track_id, PerformanceEffect::DUCK, 0);
  s.renderPeak(1024); // past the ramp
  float ducked = s.renderPeak(4096);
  CHECK_NEAR(ducked / before, 0.25f, 0.02f);
  CHECK_NEAR(s.info().getLiveSendMain(), 1.0f, 1e-6f);

  s.state.setPerformance(s.track_id, PerformanceEffect::DUCK, 127);
  s.renderPeak(1024);
  CHECK(s.renderPeak(4096) < 1e-4f);

  s.state.setPerformance(s.track_id, PerformanceEffect::DUCK, performance::kRelease);
  s.renderPeak(1024);
  CHECK_NEAR(s.renderPeak(4096) / before, 1.0f, 0.02f);
}

// Throw A sends a track whose Send A is off to the bus while held.
TEST(performance_throw_a_sends_to_the_bus_while_held) {
  SineSong s;
  CHECK(s.auxAPeak(2048) == 0.0f);

  s.state.setPerformance(s.track_id, PerformanceEffect::THROW_A, 0);
  s.auxAPeak(1024);
  CHECK(s.auxAPeak(2048) > 0.01f);
  CHECK(s.info().getLiveSendA() < 1e-6f); // the song's Send A is untouched

  s.state.setPerformance(s.track_id, PerformanceEffect::THROW_A, performance::kRelease);
  s.auxAPeak(1024);
  CHECK(s.auxAPeak(2048) == 0.0f);
}

// Orbit turns the sound around the listener and back; the track's own
// azimuth stays where the song has it. A source straight ahead has nothing on
// the left-right channel (ACN 1).
TEST(performance_orbit_turns_the_sound_and_returns_it) {
  SineSong s;
  CHECK(s.renderPeak(2048, 1) < 1e-3f);

  s.state.setPerformance(s.track_id, PerformanceEffect::ORBIT, 0);
  float turning = s.renderPeak(8192, 1);
  CHECK(turning > 0.01f);
  CHECK_NEAR(s.info().getLiveAzimuth(), 0.0f, 1e-6f);

  s.state.setPerformance(s.track_id, PerformanceEffect::ORBIT, performance::kRelease);
  s.renderPeak(44100 / 2); // past the turn back
  CHECK(s.renderPeak(2048, 1) < 1e-3f);
}

// Roll re-fires a note that starts while it is held, from the note's own
// start, long after the note itself has died away.
TEST(performance_roll_refires_a_note_started_while_held) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(string(TESTS_FIXTURES_DIR) + "/decaying_note.xml", provider));

  // The peak of the last quarter of a second's render, with Roll pressed
  // before the note (or after it, once it has started), or not at all.
  enum class Press { NEVER, BEFORE_NOTE, AFTER_NOTE };
  auto render_tail = [&](Press press) {
    ChannelConfiguration config(44100, 1);
    auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);
    int track_id = song.getPlayableTrackIds().front();
    if (press == Press::BEFORE_NOTE) state.setPerformance(track_id, PerformanceEffect::ROLL, 0);
    float tail_peak = 0.0f;
    // One second; the note itself is gone after a tenth of one.
    for (int done = 0; done < 44100; done += 256) {
      state.renderBlock(256, song, *mixer);
      if (done == 2048 && press == Press::AFTER_NOTE) state.setPerformance(track_id, PerformanceEffect::ROLL, 0);
      auto & bus = mixer->getRawBus();
      if (done < 44100 * 3 / 4 || !bus.hasChannel(Channel::Main)) continue;
      auto * data = bus.getChannelData(0);
      for (int i = 0; i < 256; i++) tail_peak = max(tail_peak, fabsf(data[i]));
    }
    return tail_peak;
  };

  CHECK(render_tail(Press::NEVER) < 1e-4f);
  CHECK(render_tail(Press::BEFORE_NOTE) > 0.01f);
  CHECK(render_tail(Press::AFTER_NOTE) < 1e-4f); // a note already playing isn't rolled
}

// A live note started while Roll is held rolls until it is stopped.
TEST(performance_roll_refires_a_live_note_until_it_stops) {
  SineSong s;
  s.state.setPerformance(s.track_id, PerformanceEffect::ROLL, 0);
  s.state.liveNoteStarted(s.track_id, 0, Tuning::EDO12, 0.8f, 60, 0, NoteOverride{});
  s.state.renderBlock(256, s.song, *s.mixer);
  CHECK(s.state.rollingNoteCount(s.track_id) == 1);
  s.state.liveNoteStopped(s.track_id, 0);
  CHECK(s.state.rollingNoteCount(s.track_id) == 0);
  s.state.liveNoteStarted(s.track_id, 0, Tuning::EDO12, 0.8f, 60, 0, NoteOverride{});
  s.state.setPerformance(s.track_id, PerformanceEffect::ROLL, performance::kRelease);
  CHECK(s.state.rollingNoteCount(s.track_id) == 0);
}

// TrackStutter passes the live signal through while it captures, then
// repeats what it captured, and fades back to the live signal on release.
TEST(track_stutter_repeats_its_capture_and_returns_to_live) {
  TrackStutter stutter;
  const int slice = 100, fade = 4, block = 50;
  stutter.start(0, slice, 400, 1, fade);
  vector<float> out;
  int frame = 0;
  auto run = [&](int blocks) {
    for (int b = 0; b < blocks; b++) {
      AudioBuffer data(1, false, false, block);
      for (int i = 0; i < block; i++) data.getChannelData(0)[i] = static_cast<float>(frame + i);
      stutter.process(data);
      CHECK(data.hasChannel(Channel::Main));
      for (int i = 0; i < block; i++) out.push_back(data.getChannelData(0)[i]);
      frame += block;
    }
  };
  run(8); // 400 frames: the capture, then three repeats
  for (int f = 0; f < slice - fade; f++) CHECK_NEAR(out[static_cast<size_t>(f)], static_cast<float>(f), 1e-6f);
  for (int repeat = 1; repeat < 4; repeat++) {
    for (int k = fade; k < slice - fade; k++) CHECK_NEAR(out[static_cast<size_t>(repeat * slice + k)], static_cast<float>(k), 1e-6f);
  }
  stutter.release();
  run(2);
  CHECK(stutter.isOff());
  CHECK_NEAR(out.back(), static_cast<float>(frame - 1), 1e-6f); // live again
}

// Stutter pressed before a short note keeps repeating it long after the note
// itself has died away.
TEST(performance_stutter_repeats_the_track_after_its_note_ends) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(string(TESTS_FIXTURES_DIR) + "/decaying_note.xml", provider));

  auto render_tail = [&](bool stutter) {
    ChannelConfiguration config(44100, 1);
    auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);
    int track_id = song.getPlayableTrackIds().front();
    if (stutter) state.setPerformance(track_id, PerformanceEffect::STUTTER, 0);
    float tail_peak = 0.0f;
    for (int done = 0; done < 44100; done += 256) {
      state.renderBlock(256, song, *mixer);
      auto & bus = mixer->getRawBus();
      if (done < 44100 * 3 / 4 || !bus.hasChannel(Channel::Main)) continue;
      auto * data = bus.getChannelData(0);
      for (int i = 0; i < 256; i++) tail_peak = max(tail_peak, fabsf(data[i]));
    }
    return tail_peak;
  };

  CHECK(render_tail(false) < 1e-4f);
  CHECK(render_tail(true) > 0.01f);
}
