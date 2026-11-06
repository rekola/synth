#include "TestFramework.h"

#include "../src/audio/AudioBuffer.h"
#include "../src/model/Clip.h"
#include "../src/model/SampleContent.h"
#include "../src/model/SampleTrack.h"
#include "../src/state/MemoryParameterSource.h"
#include "../src/state/SampleTrackState.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace std;

namespace {

constexpr int kRate = 48000;
constexpr int kFrames = kRate / 2; // half a second

shared_ptr<AudioBuffer> sine(double hz) {
  auto buffer = make_shared<AudioBuffer>(1, kFrames);
  auto data = buffer->getChannelData(0);
  for (int i = 0; i < kFrames; i++) data[i] = 0.5f * static_cast<float>(sin(2.0 * M_PI * hz * i / kRate));
  return buffer;
}

struct Played {
  double hz = 0.0; // over the middle of the clip
  int length = 0;  // frames until it falls silent
};

// A 440 Hz sine recorded at `recorded_hz`, triggered at `pitch_cents` from 440.
Played play(float recorded_hz, double pitch_cents) {
  ChannelConfiguration config(kRate);
  SampleTrackState state(config, false, false, 0, SphericalPosition{}, SendLevels{});
  Clip clip(0);
  auto & content = clip.getSampleContent();
  content.setBuffer(sine(440.0));
  content.setNativeSampleRate(kRate);
  content.setOriginalStandardPitch(recorded_hz);
  clip.setLooping(false);
  state.triggerClip(clip, 120, 0, pitch_cents);
  auto out = state.renderVoices(kFrames * 2);
  // The left channel of the decoded voice: a W-channel look is enough here.
  const float * d = out.getChannelData(0);
  Played p;
  for (int i = 0; i < out.numberOfFrames(); i++)
    if (fabsf(d[i]) > 1e-4f) p.length = i + 1;
  vector<double> crossings;
  for (int i = kFrames / 4; i < kFrames * 3 / 4; i++)
    if (d[i - 1] < 0 && d[i] >= 0) crossings.push_back(i - 1 + static_cast<double>(-d[i - 1]) / static_cast<double>(d[i] - d[i - 1]));
  if (crossings.size() > 2) p.hz = kRate * static_cast<double>(crossings.size() - 1) / (crossings.back() - crossings.front());
  return p;
}

} // namespace

TEST(sample_shift_is_the_difference_to_the_recorded_standard_pitch) {
  SampleContent content;
  CHECK(content.getOriginalStandardPitch() == 440.0f);
  CHECK(sampleShiftCents(content, 0.0) == 0.0);
  CHECK_NEAR(sampleShiftCents(content, 1200.0), 1200.0, 1e-9);
  content.setOriginalStandardPitch(415.0f);
  CHECK_NEAR(sampleShiftCents(content, 0.0), round(1200.0 * log2(440.0 / 415.0) * 10.0) / 10.0, 1e-9);
  content.setOriginalStandardPitch(0.0f); // ignored
  CHECK(content.getOriginalStandardPitch() == 415.0f);
  CHECK(sampleShiftCents(content, 1e9) == 7200.0); // only the shift is held
  CHECK(sampleShiftCents(content, NAN) == 0.0);
}

TEST(sample_recorded_at_the_songs_pitch_plays_unchanged) {
  auto p = play(440.0f, 0.0);
  CHECK_NEAR(p.hz, 440.0, 0.5);
  CHECK(abs(p.length - kFrames) < 10);
}

// The song an octave up (standard pitch 880, or a YU offset of one): the
// sample follows it, at the same length.
TEST(sample_is_pitch_shifted_to_the_songs_pitch_and_keeps_its_length) {
  auto p = play(440.0f, 1200.0);
  CHECK_NEAR(p.hz, 880.0, 3.0);
  CHECK(abs(p.length - kFrames) < kFrames / 20);
}

// Recorded at A4 = 880, played in a 440 song: an octave down.
TEST(sample_recorded_at_another_standard_pitch_is_brought_to_the_songs) {
  auto p = play(880.0f, 0.0);
  CHECK_NEAR(p.hz, 220.0, 1.5);
  CHECK(abs(p.length - kFrames) < kFrames / 20);
}

TEST(sample_standard_pitch_round_trips_and_defaults_to_440) {
  SampleContent content;
  content.setOriginalStandardPitch(432.5f);
  MemoryParameterSource params;
  content.storeParameters(params);
  SampleContent loaded;
  loaded.loadParameters(params);
  CHECK_NEAR(loaded.getOriginalStandardPitch(), 432.5f, 1e-4f);

  MemoryParameterSource old_params; // a layer saved before the attribute existed
  SampleContent old;
  old.loadParameters(old_params);
  CHECK(old.getOriginalStandardPitch() == 440.0f);
}
