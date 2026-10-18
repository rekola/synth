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
#include "../src/state/TrackGrains.h"
#include "../src/state/TrackShuffle.h"
#include "../src/state/TrackGate.h"
#include "../src/state/TrackCrush.h"
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

// The default set, top to bottom: Stutter, Shuffle, Erode, Shatter, Gate,
// Crush, Filter, Duck - the rows not built yet stay empty.
TEST(performance_grid_rows_are_the_default_set) {
  CHECK(performance::effectForGridRow(7) == PerformanceEffect::STUTTER);
  CHECK(performance::effectForGridRow(6) == PerformanceEffect::SHUFFLE);
  CHECK(performance::effectForGridRow(5) == PerformanceEffect::ERODE);
  CHECK(performance::effectForGridRow(4) == PerformanceEffect::SHATTER);
  CHECK(performance::effectForGridRow(3) == PerformanceEffect::GATE);
  CHECK(performance::effectForGridRow(2) == PerformanceEffect::CRUSH);
  CHECK(performance::effectForGridRow(1) == PerformanceEffect::NONE); // filter
  CHECK(performance::effectForGridRow(0) == PerformanceEffect::DUCK);
  CHECK(performance::effectForGridRow(8) == PerformanceEffect::NONE);
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

namespace {

// Runs a front-facing sine (first order: W and X equal, Y silent) through
// TrackGrains for `seconds`, after a quarter second to settle, and returns the
// share of frames where W is silent, the loudest W and the loudest Y.
struct ErodeRun {
  float silent_share = 0.0f, peak_w = 0.0f, peak_y = 0.0f;
  bool finite = true;
};
ErodeRun runErode(TrackGrains & erode, int & frame, float seconds) {
  const int rate = 44100, block = 256;
  ErodeRun run;
  int silent = 0, counted = 0;
  const int blocks = static_cast<int>(seconds * rate) / block;
  for (int b = 0; b < blocks; b++) {
    AudioBuffer data(4, false, false, block);
    for (int i = 0; i < block; i++) {
      float v = 0.5f * sinf(2.0f * 3.14159265f * 220.0f * static_cast<float>(frame + i) / rate);
      data.getChannelData(0)[i] = v;
      data.getChannelData(1)[i] = 0.0f;
      data.getChannelData(2)[i] = 0.0f;
      data.getChannelData(3)[i] = v;
    }
    erode.process(data);
    for (int i = 0; i < block; i++) {
      float w = data.getChannelData(0)[i], y = data.getChannelData(1)[i];
      run.finite = run.finite && std::isfinite(w) && std::isfinite(y);
      run.peak_w = max(run.peak_w, fabsf(w));
      run.peak_y = max(run.peak_y, fabsf(y));
      silent += fabsf(w) < 1e-6f ? 1 : 0;
      counted++;
    }
    frame += block;
  }
  run.silent_share = static_cast<float>(silent) / static_cast<float>(max(counted, 1));
  return run;
}

} // namespace

// Fully eroded, the sound comes through in fragments with silence between
// them, turned a little off the front; lightly, it is mostly there.
TEST(track_erode_breaks_the_sound_into_patches) {
  int frame = 0;
  TrackGrains heavy(grains::kErode, 3);
  heavy.start(44100, 4, 1.0f);
  runErode(heavy, frame, 0.25f);
  auto eroded = runErode(heavy, frame, 2.0f);
  CHECK(eroded.finite);
  CHECK(eroded.silent_share > 0.2f);
  CHECK(eroded.peak_w > 0.05f);
  CHECK(eroded.peak_y > 1e-3f); // turned

  frame = 0;
  TrackGrains light(grains::kErode, 3);
  light.start(44100, 4, 0.0f);
  runErode(light, frame, 0.25f);
  auto grainy = runErode(light, frame, 2.0f);
  CHECK(grainy.silent_share < eroded.silent_share);
}

// Released, Erode fades back to the track's own signal, unchanged.
TEST(track_erode_hands_the_sound_back_on_release) {
  int frame = 0;
  TrackGrains erode(grains::kErode, 5);
  erode.start(44100, 4, 0.5f);
  runErode(erode, frame, 0.5f);
  erode.release();
  runErode(erode, frame, 0.05f);
  CHECK(erode.isOff());
  auto after = runErode(erode, frame, 0.1f);
  CHECK(after.silent_share < 0.01f);
  CHECK(after.peak_y == 0.0f);
}

// Held from the Custom grid, Erode reaches the track.
TEST(performance_erode_reaches_the_track) {
  SineSong s;
  float clean = s.renderPeak(4096, 0);
  s.state.setPerformance(s.track_id, PerformanceEffect::ERODE, 127);
  s.renderPeak(44100 / 4);
  float eroded = s.renderPeak(44100);
  CHECK(std::isfinite(eroded));
  CHECK(fabsf(eroded - clean) > 1e-3f);
  s.state.setPerformance(s.track_id, PerformanceEffect::ERODE, performance::kRelease);
  s.renderPeak(4096);
  CHECK_NEAR(s.renderPeak(4096, 0), clean, 1e-4f);
}

// Duck held on the master dips the whole mix, and gives it back on release.
TEST(performance_master_duck_dips_the_whole_mix) {
  SineSong s;
  float before = s.renderPeak(4096);
  s.state.setPerformance(performance::kMasterTarget, PerformanceEffect::DUCK, 0);
  s.renderPeak(1024);
  CHECK_NEAR(s.renderPeak(4096) / before, 0.25f, 0.02f);
  s.state.setPerformance(performance::kMasterTarget, PerformanceEffect::DUCK, performance::kRelease);
  s.renderPeak(1024);
  CHECK_NEAR(s.renderPeak(4096) / before, 1.0f, 0.02f);
}

// Stutter held on the master repeats the whole mix after its note has died
// away, and Erode on the master breaks it up; neither leaves anything behind
// once released.
TEST(performance_master_stutter_and_erode_act_on_the_mix) {
  InstrumentProvider provider;
  Song song;
  CHECK(song.open(string(TESTS_FIXTURES_DIR) + "/decaying_note.xml", provider));

  auto render_tail = [&](PerformanceEffect effect) {
    ChannelConfiguration config(44100, 1);
    auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);
    if (effect != PerformanceEffect::NONE) state.setPerformance(performance::kMasterTarget, effect, 0);
    float tail_peak = 0.0f;
    bool finite = true;
    for (int done = 0; done < 44100; done += 256) {
      state.renderBlock(256, song, *mixer);
      auto & bus = mixer->getRawBus();
      if (!bus.hasChannel(Channel::Main)) continue;
      auto * data = bus.getChannelData(0);
      for (int i = 0; i < 256; i++) finite = finite && std::isfinite(data[i]);
      if (done < 44100 * 3 / 4) continue;
      for (int i = 0; i < 256; i++) tail_peak = max(tail_peak, fabsf(data[i]));
    }
    CHECK(finite);
    return tail_peak;
  };

  CHECK(render_tail(PerformanceEffect::NONE) < 1e-4f);
  CHECK(render_tail(PerformanceEffect::STUTTER) > 0.01f);
  render_tail(PerformanceEffect::ERODE); // finite throughout

  SineSong s;
  float clean = s.renderPeak(4096, 0);
  s.state.setPerformance(performance::kMasterTarget, PerformanceEffect::ERODE, 127);
  s.renderPeak(44100 / 2);
  s.state.setPerformance(performance::kMasterTarget, PerformanceEffect::ERODE, performance::kRelease);
  s.renderPeak(4096);
  CHECK_NEAR(s.renderPeak(4096, 0), clean, 1e-4f);
}

// Shatter fills the gaps Erode leaves: a dense spray, scattered all round.
TEST(track_shatter_is_dense_and_scattered) {
  int frame = 0;
  TrackGrains eroded(grains::kErode, 3), shattered(grains::kShatter, 3);
  eroded.start(44100, 4, 1.0f);
  shattered.start(44100, 4, 1.0f);
  runErode(eroded, frame, 1.25f);
  frame = 0;
  runErode(shattered, frame, 1.25f);
  auto e = runErode(eroded, frame, 2.0f);
  auto sh = runErode(shattered, frame, 2.0f);
  CHECK(sh.finite);
  CHECK(sh.silent_share < e.silent_share);
  CHECK(sh.peak_y > 0.05f);
  CHECK(sh.peak_w < 4.0f); // the feedback stays bounded
  shattered.release();
  runErode(shattered, frame, 0.05f);
  CHECK(shattered.isOff());
}

// Shuffle starts as the live sound, then plays slices of what it has
// recorded in a new order, each slice whole and none from the future, and
// hands the sound back on release.
TEST(track_shuffle_plays_recorded_slices_in_a_new_order) {
  const int slice = 100, block = 256;
  TrackShuffle shuffle(7);
  shuffle.start(0, slice, 1000, 1, 1);
  int frame = 0;
  bool live_first = true, whole = true, past = true, shuffled = false;
  float slice_first = 0.0f;
  for (int b = 0; b < 20; b++) {
    AudioBuffer data(1, false, false, block);
    for (int i = 0; i < block; i++) data.getChannelData(0)[i] = static_cast<float>(frame + i); // a ramp: each frame names itself
    shuffle.process(data);
    for (int i = 0; i < block; i++, frame++) {
      float v = data.getChannelData(0)[i];
      if (frame < slice) live_first = live_first && v == static_cast<float>(frame);
      past = past && v <= static_cast<float>(frame);
      if (frame % slice == 0)
        slice_first = v;
      else
        whole = whole && v - slice_first == static_cast<float>(frame % slice);
      shuffled = shuffled || v != static_cast<float>(frame);
    }
  }
  CHECK(live_first);
  CHECK(past);
  CHECK(whole);
  CHECK(shuffled);
  shuffle.release();
  AudioBuffer data(1, false, false, block);
  for (int i = 0; i < block; i++) data.getChannelData(0)[i] = static_cast<float>(frame + i);
  shuffle.process(data);
  CHECK(shuffle.isOff());
  CHECK(data.getChannelData(0)[block - 1] == static_cast<float>(frame + block - 1));
}

// The gate is open for the first half of its period and shut for the
// second, from where the grid is.
TEST(track_gate_opens_on_the_grid) {
  TrackGate gate;
  gate.start(200, 0, 1);
  AudioBuffer data(1, false, false, 400);
  for (int i = 0; i < 400; i++) data.getChannelData(0)[i] = 1.0f;
  gate.process(data);
  auto * d = data.getChannelData(0);
  CHECK(d[0] == 1.0f && d[99] == 1.0f && d[100] == 0.0f && d[199] == 0.0f && d[200] == 1.0f);
  gate.setPeriod(200, 150); // the grid three quarters through a period: shut
  AudioBuffer more(1, false, false, 100);
  for (int i = 0; i < 100; i++) more.getChannelData(0)[i] = 1.0f;
  gate.process(more);
  CHECK(more.getChannelData(0)[10] == 0.0f && more.getChannelData(0)[60] == 1.0f);
  gate.release();
  gate.process(more);
  CHECK(gate.isOff());
}

// Fully crushed, the sound is held for 32 frames at a time and on a grid of
// eighths.
TEST(track_crush_holds_and_quantizes) {
  TrackCrush crush;
  crush.start(44100, 1.0f);
  const int frames = 8192;
  AudioBuffer data(1, false, false, frames);
  for (int i = 0; i < frames; i++) data.getChannelData(0)[i] = 0.7f * sinf(2.0f * 3.14159265f * 300.0f * static_cast<float>(i) / 44100.0f);
  crush.process(data);
  auto * d = data.getChannelData(0);
  int changes = 0;
  bool on_grid = true;
  for (int i = 1024; i < frames; i++) {
    changes += d[i] != d[i - 1] ? 1 : 0;
    on_grid = on_grid && fabsf(d[i] * 8.0f - roundf(d[i] * 8.0f)) < 1e-4f;
  }
  CHECK(on_grid);
  CHECK(changes <= (frames - 1024) / 32 + 1);
  CHECK(changes > 0);
}

// Each new sound effect, held on a track or on the master, changes what is
// heard, and leaves nothing behind once released.
TEST(performance_sound_effects_reach_the_track_and_the_master) {
  for (int target : {0, performance::kMasterTarget}) {
    for (auto effect : {PerformanceEffect::SHUFFLE, PerformanceEffect::SHATTER, PerformanceEffect::GATE, PerformanceEffect::CRUSH}) {
      SineSong held, clean;
      auto max_difference = [&](int frames) {
        float diff = 0.0f;
        bool finite = true;
        for (int done = 0; done < frames; done += 256) {
          held.state.renderBlock(256, held.song, *held.mixer);
          clean.state.renderBlock(256, clean.song, *clean.mixer);
          auto & a = held.mixer->getRawBus();
          auto & b = clean.mixer->getRawBus();
          if (!a.hasChannel(Channel::Main) || !b.hasChannel(Channel::Main)) continue;
          for (int i = 0; i < 256; i++) {
            finite = finite && std::isfinite(a.getChannelData(0)[i]);
            diff = max(diff, fabsf(a.getChannelData(0)[i] - b.getChannelData(0)[i]));
          }
        }
        CHECK(finite);
        return diff;
      };
      const int track = target == 0 ? held.track_id : target;
      held.state.setPerformance(track, effect, 127);
      CHECK(max_difference(44100 / 2) > 1e-3f);
      held.state.setPerformance(track, effect, performance::kRelease);
      max_difference(4096);
      CHECK(max_difference(4096) < 1e-4f);
    }
  }
}
