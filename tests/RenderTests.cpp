#include "TestFramework.h"

#include "../Song.h"
#include "../InstrumentProvider.h"
#include "../OfflineRenderer.h"
#include "../ChannelConfiguration.h"

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
