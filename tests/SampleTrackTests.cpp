#include "TestFramework.h"

#include "../src/audio/SampleFileLoader.h"
#include "../src/audio/AudioBuffer.h"
#include "../src/dsp/Resampler.h"
#include "../src/model/Clip.h"
#include "../src/model/SampleContent.h"
#include "../src/state/SampleTrackState.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include <functional>
#include <memory>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif

using namespace std;

TEST(load_mono_sample_downmixes_every_channel_rather_than_truncating_to_channel_0) {
  // Fixture: 50 frames of constant stereo (left=0.6, right=0.2) - if this
  // truncated to channel 0 (the old, removed FileInstrument::openFile()
  // behavior) it would read back 0.6; a genuine average reads back 0.4.
  auto loaded = loadMonoSample(string(TESTS_FIXTURES_DIR) + "/sample_track_stereo_downmix.wav");
  CHECK(loaded.buffer != nullptr);
  if (!loaded.buffer) return;

  CHECK(loaded.rate == 8000);
  CHECK(loaded.buffer->numberOfFrames() == 50);
  auto data = loaded.buffer->getChannelData(0);
  for (int i = 0; i < 50; i++) CHECK_NEAR(data[i], 0.4f, 0.01f);
}

TEST(load_mono_sample_fails_gracefully_on_a_missing_file) {
  auto loaded = loadMonoSample(string(TESTS_FIXTURES_DIR) + "/does_not_exist.wav");
  CHECK(loaded.buffer == nullptr);
  CHECK(loaded.rate == 0);
}

TEST(resample_mono_linear_scales_the_frame_count_by_the_rate_ratio) {
  vector<float> input(100, 0.5f);
  auto up = resampleMonoLinear(input, 8000, 16000); // half the rate -> twice the frames
  CHECK(up.size() == 200);
  auto down = resampleMonoLinear(input, 16000, 8000); // twice the rate -> half the frames
  CHECK(down.size() == 50);
  for (auto v : up) CHECK_NEAR(v, 0.5f, 1e-5f); // a constant signal resamples to the same constant
}

TEST(resample_mono_linear_interpolates_a_ramp_correctly) {
  vector<float> input;
  for (int i = 0; i < 10; i++) input.push_back(static_cast<float>(i));
  auto out = resampleMonoLinear(input, 10, 20); // upsample 2x - every other output sample lands exactly on an input sample
  CHECK(out.size() == 20);
  for (size_t i = 0; i < out.size(); i += 2) CHECK_NEAR(out[i], static_cast<float>(i) / 2.0f, 1e-4f);
}

TEST(resample_mono_linear_is_a_no_op_for_degenerate_input) {
  vector<float> input(10, 1.0f);
  CHECK(resampleMonoLinear(input, 0, 8000).size() == input.size());
  CHECK(resampleMonoLinear(input, 8000, 0).size() == input.size());
  CHECK(resampleMonoLinear(input, 8000, 8000).size() == input.size());
  CHECK(resampleMonoLinear({}, 8000, 16000).empty());
}

TEST(clip_has_no_sample_content_until_first_asked_for_one) {
  Clip clip(0);
  CHECK(!clip.hasSample());
  CHECK(clip.getSampleContent() == nullptr);

  auto & content = clip.getOrCreateSampleContent();
  CHECK(!clip.hasSample()); // created, but still no buffer of its own
  content.setBuffer(make_shared<AudioBuffer>(1, 4));
  CHECK(clip.hasSample());
  CHECK(clip.getSampleContent() == &content); // getOrCreateSampleContent() never replaces an existing one
}

namespace {

// Builds a mono buffer of `frames` samples, each set from `value_at(i)`.
shared_ptr<AudioBuffer> buildBuffer(int frames, const std::function<float(int)> & value_at) {
  auto buffer = make_shared<AudioBuffer>(1, frames);
  auto data = buffer->getChannelData(0);
  for (int i = 0; i < frames; i++) data[i] = value_at(i);
  return buffer;
}

}

TEST(trigger_clip_plays_only_the_trimmed_range_and_ends_without_a_ramp) {
  ChannelConfiguration config(8000); // no resampling - native rate matches
  SampleTrackState state(config, false, false, 0, SphericalPosition{}, SendLevels{});

  Clip clip(0);
  auto & content = clip.getOrCreateSampleContent();
  content.setBuffer(buildBuffer(40, [](int) { return 0.5f; }));
  content.setNativeSampleRate(8000);
  content.setInPoint(10.0f / 8000.0f); // trims the first 10 frames
  content.setOutPoint(10.0f / 8000.0f); // trims the last 10 frames - [10, 30) remains, 20 frames
  clip.setLooping(false);

  state.triggerClip(clip);
  auto rendered = state.renderVoices(40); // more than the trimmed range - the tail must read back silent
  auto out = rendered.getChannelData(0);

  CHECK(out[0] != 0.0f); // the trimmed-in content is actually sounding
  for (int i = 1; i < 20; i++) CHECK_NEAR(out[i], out[0], 1e-6f); // constant source, constant (unmoving) position -> constant output
  for (int i = 20; i < 40; i++) CHECK(out[i] == 0.0f); // one-shot ended exactly at the trimmed length, no fade needed for a natural end
}

TEST(trigger_clip_loops_back_to_the_in_point_not_frame_0) {
  ChannelConfiguration config(8000);
  SampleTrackState state(config, false, false, 0, SphericalPosition{}, SendLevels{});

  Clip clip(0);
  auto & content = clip.getOrCreateSampleContent();
  // [0, 10): a marker value that must never be heard - looping to frame 0
  // instead of the in-point would read it back. [10, 20): an ascending
  // ramp, distinct enough to tell "start of a lap" apart from "midway
  // through one".
  content.setBuffer(buildBuffer(20, [](int i) { return i < 10 ? -1.0f : 100.0f + static_cast<float>(i - 10); }));
  content.setNativeSampleRate(8000);
  content.setInPoint(10.0f / 8000.0f);
  clip.setLooping(true);

  state.triggerClip(clip);
  auto rendered = state.renderVoices(25); // two full laps of the trimmed 10-frame range plus a partial third
  auto out = rendered.getChannelData(0);

  CHECK(out[0] != 0.0f);
  CHECK(out[1] != out[0]); // distinct source samples must not collapse to the same output
  CHECK_NEAR(out[10], out[0], 1e-6f); // the second lap's first frame reads the same source sample as the first lap's - the in-point, not frame 0's own marker value
  CHECK_NEAR(out[20], out[0], 1e-6f); // and so does the third
}

TEST(trigger_clip_falls_back_to_the_full_buffer_when_trim_points_overshoot) {
  ChannelConfiguration config(8000);
  SampleTrackState state(config, false, false, 0, SphericalPosition{}, SendLevels{});

  Clip clip(0);
  auto & content = clip.getOrCreateSampleContent();
  content.setBuffer(buildBuffer(10, [](int i) { return 10.0f + static_cast<float>(i); })); // distinct, all nonzero
  content.setNativeSampleRate(8000);
  content.setInPoint(100.0f); // 100s in, on a 10-frame/8kHz buffer - hopelessly past the end
  clip.setLooping(false);

  state.triggerClip(clip);
  auto rendered = state.renderVoices(15);
  auto out = rendered.getChannelData(0);

  CHECK(out[0] != 0.0f); // played from frame 0, not silence
  CHECK(out[9] != 0.0f); // and ran the full 10 frames, not just up to the (invalid) in-point
  for (int i = 10; i < 15; i++) CHECK(out[i] == 0.0f); // one-shot, ended after the real buffer length
}
