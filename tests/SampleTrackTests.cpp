#include "TestFramework.h"

#include "../src/audio/SampleFileLoader.h"
#include "../src/audio/AudioBuffer.h"
#include "../src/dsp/Resampler.h"
#include "../src/model/Clip.h"
#include "../src/model/SampleContent.h"
#include "../src/state/LeafTrackState.h"
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

// SampleClipVoice has no looping concept of its own any more (see
// SampleTrackState::triggerClip()'s own comment) - a looping clip's own
// lap repeat is realized by the *caller* invoking triggerClip() again,
// fresh, once the row grid says it's time (SongState.h's own scheduling,
// LaunchpadManager::fireOrTriggerClipStep()), not by one voice looping
// internally. This directly exercises that: two separate triggerClip()
// calls on the same still-playing track, simulating what the caller's
// own per-lap re-trigger actually does - the second one must both cut
// the first voice's own tail short (stopVoices(0), a short release fade)
// and start the new one from the in-point, not frame 0. The buffer is
// long enough, and the retrigger early enough, that the check below sits
// comfortably past the first voice's own short release tail (kReleaseSeconds,
// SampleTrack.cpp) - otherwise the two voices' own output would still be
// crossfading together at that point, not cleanly attributable to either.
TEST(retriggering_a_clip_starts_the_new_voice_at_the_in_point_not_frame_0) {
  ChannelConfiguration config(8000);
  SampleTrackState state(config, false, false, 0, SphericalPosition{}, SendLevels{});

  Clip clip(0);
  auto & content = clip.getOrCreateSampleContent();
  // [0, 100): a marker value that must never be heard - a fresh trigger
  // starting at frame 0 instead of the in-point would read it back.
  // [100, 300): an ascending ramp, distinct enough to tell "start of a
  // lap" apart from "midway through one".
  content.setBuffer(buildBuffer(300, [](int i) { return i < 100 ? -1.0f : 100.0f + static_cast<float>(i - 100); }));
  content.setNativeSampleRate(8000);
  content.setInPoint(100.0f / 8000.0f);
  clip.setLooping(true);

  state.triggerClip(clip);
  auto first_lap = state.renderVoices(3); // still early, well short of its own natural end
  auto first_out = first_lap.getChannelData(0);
  CHECK(first_out[0] != 0.0f);

  state.triggerClip(clip); // the caller's own re-trigger, as if a new lap just started
  auto after_retrigger = state.renderVoices(200); // comfortably longer than the old voice's own release tail
  auto out2 = after_retrigger.getChannelData(0);
  CHECK_NEAR(out2[150], 250.0f, 1e-3f); // the new voice's own in-point-based position (100 + 150), not frame 0's own marker value, and no longer mixed with the old voice's own (by now fully released) tail
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

// SampleTrackState derives from LeafTrackState directly (not
// InstrumentTrackState - see that class's own doc comment), the same
// shared base InstrumentTrackState derives from - Player.cpp's
// SET_TRACK_MUTED/SOLO/SEND_A/SEND_B/SEND_MAIN/AZIMUTH and STOP_ALL_NOTES
// handlers all dynamic_cast to LeafTrackState so those live controls (and
// Launchpad Session view's "stop this track") keep reaching a SampleTrack
// exactly like they reach every other leaf track type. Exercised here
// through a real LeafTrackState& reference, the same shape those
// dynamic_casts resolve to, rather than SampleTrackState's own name.
TEST(sample_track_state_live_controls_are_reachable_through_leaf_track_state) {
  ChannelConfiguration config(8000);
  SampleTrackState state(config, false, false, 0, SphericalPosition{}, SendLevels{});
  LeafTrackState & leaf = state;

  leaf.setMuted(true);
  CHECK(state.isMuted());
  leaf.setSolo(true);
  CHECK(state.isSolo());
  leaf.setAzimuth(45.0f);
  CHECK_NEAR(state.getAzimuth(), 45.0f, 1e-4f);
}

TEST(stop_all_voices_releases_a_sample_track_through_the_leaf_track_state_base) {
  ChannelConfiguration config(8000);
  SampleTrackState state(config, false, false, 0, SphericalPosition{}, SendLevels{});

  Clip clip(0);
  auto & content = clip.getOrCreateSampleContent();
  content.setBuffer(buildBuffer(4000, [](int) { return 0.5f; }));
  content.setNativeSampleRate(8000);
  clip.setLooping(false);

  state.triggerClip(clip);
  CHECK(state.isActive());

  static_cast<LeafTrackState &>(state).stopAllVoices();

  auto rendered = state.renderVoices(1000); // comfortably longer than the short release fade
  auto out = rendered.getChannelData(0);
  CHECK(out[999] == 0.0f); // fully released well before the buffer's own natural end
  CHECK(!state.isActive());
}

TEST(waveform_peaks_is_empty_without_a_buffer) {
  Clip clip(0);
  clip.setLength(4);
  auto & peaks = clip.getWaveformPeaks(3);
  CHECK(peaks.empty());
  CHECK(peaks.rowCount() == 0);
}

TEST(waveform_peaks_builds_row_count_times_subrows_buckets) {
  Clip clip(0);
  auto & content = clip.getOrCreateSampleContent();
  content.setBuffer(buildBuffer(80, [](int i) { return 0.5f + 0.01f * static_cast<float>(i % 5); }));
  content.setNativeSampleRate(8000);
  clip.setLength(4);

  auto & peaks = clip.getWaveformPeaks(3);
  CHECK(!peaks.empty());
  CHECK(peaks.rowCount() == 4);
  CHECK(peaks.subrowsPerRow() == 3);
}

TEST(waveform_peaks_normalizes_against_its_own_loudest_bucket) {
  Clip clip(0);
  auto & content = clip.getOrCreateSampleContent();
  // 4 buckets (rowCount 2 * subrows 2): quiet, quiet, loud, quiet - the
  // loud bucket alone should read back at 1.0, the quiet ones scaled down
  // proportionally rather than against some fixed absolute reference.
  content.setBuffer(buildBuffer(40, [](int i) {
    auto bucket = i / 10;
    return bucket == 2 ? 1.0f : 0.25f;
  }));
  content.setNativeSampleRate(8000);
  clip.setLength(2);

  auto & peaks = clip.getWaveformPeaks(2);
  CHECK_NEAR(peaks.at(1, 0), 1.0f, 1e-4f); // row 1, subrow 0 = bucket index 2
  CHECK_NEAR(peaks.at(0, 0), 0.25f, 1e-4f);
  CHECK_NEAR(peaks.at(0, 1), 0.25f, 1e-4f);
  CHECK_NEAR(peaks.at(1, 1), 0.25f, 1e-4f);
}

TEST(waveform_peaks_ignores_content_outside_the_trimmed_range) {
  Clip clip(0);
  auto & content = clip.getOrCreateSampleContent();
  // A loud marker sitting entirely in the trimmed-away head/tail - if it
  // leaked into normalization, every real (post-trim) bucket would read
  // back far under 1.0 even though the trimmed range itself is uniform.
  content.setBuffer(buildBuffer(40, [](int i) { return (i < 10 || i >= 30) ? 10.0f : 0.5f; }));
  content.setNativeSampleRate(8000);
  content.setInPoint(10.0f / 8000.0f);
  content.setOutPoint(10.0f / 8000.0f);
  clip.setLength(2);

  auto & peaks = clip.getWaveformPeaks(1);
  for (int row = 0; row < 2; row++) CHECK_NEAR(peaks.at(row, 0), 1.0f, 1e-4f);
}

TEST(waveform_peaks_rebuilds_when_the_buffer_is_replaced) {
  Clip clip(0);
  auto & content = clip.getOrCreateSampleContent();
  content.setBuffer(buildBuffer(10, [](int) { return 0.2f; }));
  content.setNativeSampleRate(8000);
  clip.setLength(1);

  clip.getWaveformPeaks(1); // first build, cached

  content.setBuffer(buildBuffer(10, [](int) { return 0.9f; }));
  auto & peaks = clip.getWaveformPeaks(1);
  CHECK_NEAR(peaks.at(0, 0), 1.0f, 1e-4f); // reflects the new buffer's own content, not a stale cache
}

TEST(waveform_peaks_rebuilds_when_row_count_or_subrows_change) {
  Clip clip(0);
  auto & content = clip.getOrCreateSampleContent();
  content.setBuffer(buildBuffer(40, [](int) { return 0.5f; }));
  content.setNativeSampleRate(8000);
  clip.setLength(2);

  clip.getWaveformPeaks(2);
  CHECK(clip.getWaveformPeaks(2).subrowsPerRow() == 2);

  auto & rebuilt_subrows = clip.getWaveformPeaks(3); // a different runtime capability choice
  CHECK(rebuilt_subrows.subrowsPerRow() == 3);
  CHECK(rebuilt_subrows.rowCount() == 2);

  clip.setLength(4);
  auto & rebuilt_length = clip.getWaveformPeaks(3);
  CHECK(rebuilt_length.rowCount() == 4);
}
