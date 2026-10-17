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
    track_id = song.addTrack(make_unique<InstrumentTrack>(0)).getInternalId();
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
  CHECK(performance::effectForGridRow(6) == PerformanceEffect::NONE);
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

// Roll keeps re-firing a note that has already died away, once a row, while
// it is held.
TEST(performance_roll_refires_the_playing_note_every_row) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(string(TESTS_FIXTURES_DIR) + "/decaying_note.xml", provider));

  auto render_tail = [&](bool roll) {
    ChannelConfiguration config(44100, 1);
    auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);
    int track_id = song.getPlayableTrackIds().front();
    if (roll) state.setPerformance(track_id, PerformanceEffect::ROLL, 0);
    float tail_peak = 0.0f;
    // One second; the note itself is gone after a tenth of one.
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
