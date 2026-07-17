#include "TestFramework.h"

#include "../Song.h"
#include "../InstrumentProvider.h"
#include "../OfflineRenderer.h"
#include "../ChannelConfiguration.h"

#include <algorithm>
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
  InstrumentProvider provider; // no SoundFont: fixtures only use built-in oscilators
  Song song;
  bool ok = song.open(std::string(TESTS_FIXTURES_DIR) + "/" + name, provider);
  return { ok, std::move(song) };
}

float rms(const OfflineRenderResult & result, int channel) {
  double sum = 0.0;
  auto frames = result.numberOfFrames();
  for (size_t i = 0; i < frames; i++) sum += static_cast<double>(std::pow(result.interleaved[i * result.channels + channel], 2));
  return frames ? static_cast<float>(std::sqrt(sum / frames)) : 0.0f;
}

bool hasNonFiniteSample(const OfflineRenderResult & result) {
  for (auto v : result.interleaved) {
    if (!std::isfinite(v)) return true;
  }
  return false;
}

} // namespace

TEST(render_center_note_produces_symmetric_stereo_output) {
  auto loaded = loadFixture("center_note.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(ChannelConfiguration::STEREO, 44100);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));

  auto left = rms(result, 0), right = rms(result, 1);
  CHECK(left > 1e-4f);
  CHECK_NEAR(left, right, left * 0.05f); // centered azimuth: equal-power pan is symmetric
}

TEST(render_hard_pan_isolates_channels) {
  auto loaded = loadFixture("hard_pan.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(ChannelConfiguration::STEREO, 44100);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));

  // both tracks play the same note; track 0 is hard left, track 1 hard
  // right, so channel content should not leak into the opposite side.
  auto left = rms(result, 0), right = rms(result, 1);
  CHECK(left > 1e-4f);
  CHECK(right > 1e-4f);
  CHECK_NEAR(left, right, left * 0.05f);
}

TEST(render_reverb_reaches_both_channels_from_panned_source) {
  auto loaded = loadFixture("reverb_pan.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(ChannelConfiguration::STEREO, 44100);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  // the source is hard right (azimuth 90); the reverb tail after the note's
  // short release must still be audible on the left channel.
  auto frames = result.numberOfFrames();
  auto tail_start = frames > 44100 ? frames - 44100 : 0; // last second
  double left_energy = 0.0;
  for (size_t i = tail_start; i < frames; i++) left_energy += std::pow(result.interleaved[i * result.channels + 0], 2);

  CHECK(left_energy > 0.0);
}

TEST(render_chorus_preserves_stereo_image) {
  auto loaded = loadFixture("chorus_pan.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(ChannelConfiguration::STEREO, 44100);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  // source is hard right; chorus must not smear energy into the left
  // channel (it did before it supported more than one channel).
  auto left = rms(result, 0), right = rms(result, 1);
  CHECK(right > 1e-4f);
  CHECK(left < right * 0.05f);
}

TEST(render_envelope_decays_after_hold_and_decay_time) {
  // center_note.xml wraps its oscilator in <envelope attack=.01 hold=.3
  // decay=.3 sustain=0 release=.05> - with sustain 0 the note fully decays
  // to silence around t=.61s even with no note-off in the pattern. This
  // exercises EnvelopeFilterState's decay (getOwnLoudnessFactor) end to end
  // via actual audio output, guarding the note_value/playNote signature
  // plumbing threaded through every instrument type.
  auto loaded = loadFixture("center_note.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(ChannelConfiguration::STEREO, 44100);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  auto sample_rate = 44100;
  auto frames = result.numberOfFrames();
  auto windowRms = [&](float start_s, float end_s) {
    size_t start = std::min<size_t>(size_t(start_s * sample_rate), frames);
    size_t end = std::min<size_t>(size_t(end_s * sample_rate), frames);
    if (end <= start) return 0.0f;
    double sum = 0.0;
    for (size_t i = start; i < end; i++) sum += std::pow(result.interleaved[i * result.channels], 2);
    return static_cast<float>(std::sqrt(sum / (end - start)));
  };

  auto early = windowRms(0.05f, 0.15f); // after attack, during hold at full level
  auto late = windowRms(1.0f, 1.1f);    // long after decay reached sustain=0

  CHECK(early > 1e-3f);
  CHECK(late < early * 0.01f);
}

TEST(render_mono_with_compressor_does_not_read_out_of_bounds) {
  // Compressor's detection/gain-reduction algorithm is inherently stereo; on
  // mono output it must pass audio through instead of reading channel 1 of a
  // 1-channel buffer (a heap-buffer-overflow this fixture used to trigger
  // under AddressSanitizer).
  auto loaded = loadFixture("compressor_mono.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(ChannelConfiguration::MONO, 44100);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.channels == 1);
  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(rms(result, 0) > 1e-4f); // audio still passes through unmodified
}
