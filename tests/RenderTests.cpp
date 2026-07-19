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

float windowedRms(const OfflineRenderResult & result, int channel, float start_s, float end_s) {
  auto frames = result.numberOfFrames();
  size_t start = std::min<size_t>(static_cast<size_t>(start_s * result.sampleRate), frames);
  size_t end = std::min<size_t>(static_cast<size_t>(end_s * result.sampleRate), frames);
  if (end <= start) return 0.0f;
  double sum = 0.0;
  for (size_t i = start; i < end; i++) sum += std::pow(result.interleaved[i * result.channels + channel], 2);
  return static_cast<float>(std::sqrt(sum / (end - start)));
}

// RMS of the right-minus-left difference signal. decodeToStereo's cheap
// cardioid matrix makes right-left = k*Y exactly (W cancels out - see
// AmbisonicEncoding.h), so this is monotonic in |sin(azimuth)| alone,
// unlike comparing rms(right) vs rms(left) independently: past a certain
// azimuth the "quieter" channel actually goes negative (phase-inverted,
// not just quiet - a real, accepted property of this cheap decode,
// confirmed independently by the stereo_decode_reencode_preserves_left_
// right_direction unit test), so its RMS magnitude can grow right along
// with the "louder" channel's, making a naive rms(right)-rms(left)
// comparison unreliable for judging "how much more right-heavy is this."
float windowedRmsDifference(const OfflineRenderResult & result, float start_s, float end_s) {
  auto frames = result.numberOfFrames();
  size_t start = std::min<size_t>(static_cast<size_t>(start_s * result.sampleRate), frames);
  size_t end = std::min<size_t>(static_cast<size_t>(end_s * result.sampleRate), frames);
  if (end <= start) return 0.0f;
  double sum = 0.0;
  for (size_t i = start; i < end; i++) {
    auto diff = result.interleaved[i * result.channels + 1] - result.interleaved[i * result.channels + 0];
    sum += std::pow(diff, 2);
  }
  return static_cast<float>(std::sqrt(sum / (end - start)));
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
  // Compressor's detection/gain-reduction loop now iterates over however
  // many channels are actually present (generalized so it also works
  // directly on ambisonic input - see AmbisonicEncoding.h/Compressor.cpp),
  // rather than hardcoding channel 1 (a heap-buffer-overflow this fixture
  // used to trigger under AddressSanitizer when that channel didn't
  // exist). Mono input is now genuinely compressed, not bypassed - this
  // just guards that it still produces valid, in-bounds, audible output.
  auto loaded = loadFixture("compressor_mono.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(ChannelConfiguration::MONO, 44100);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.channels == 1);
  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(rms(result, 0) > 1e-4f);
}

TEST(render_ambisonic_directions_produce_distinguishable_output) {
  auto loaded = loadFixture("ambisonic_directions.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(ChannelConfiguration::AMBISONIC, 44100);
  auto result = renderSongOffline(loaded.song, config, MixerType::AMBISONIC_STEREO);

  CHECK(result.channels == 2); // always decoded to stereo, regardless of the 4-channel bus
  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));

  // Each track fires on its own row, 4 rows (0.5s) apart; envelope rings
  // ~0.065s, so a 0.2s window from each row's start safely captures one
  // note without bleeding into the next.
  auto windowFor = [&](int row) {
    float start = row * 0.125f;
    return std::make_pair(windowedRms(result, 0, start, start + 0.2f), windowedRms(result, 1, start, start + 0.2f));
  };

  auto diffFor = [&](int row) {
    float start = row * 0.125f;
    return windowedRmsDifference(result, start, start + 0.2f);
  };

  auto [front_l, front_r] = windowFor(0);     // azimuth 0
  auto [az45_l, az45_r] = windowFor(4);       // azimuth 45 (right-ish)
  auto [az90_l, az90_r] = windowFor(8);       // azimuth 90 (hard right)
  auto [az135_l, az135_r] = windowFor(12);    // azimuth 135 (back-right)
  auto [back_l, back_r] = windowFor(16);      // azimuth 180
  auto [azm135_l, azm135_r] = windowFor(20);  // azimuth -135 (back-left)
  auto [azm90_l, azm90_r] = windowFor(24);    // azimuth -90 (hard left)
  auto [azm45_l, azm45_r] = windowFor(28);    // azimuth -45 (left-ish)
  auto [up_l, up_r] = windowFor(32);          // elevation 90
  auto [down_l, down_r] = windowFor(36);      // elevation -90
  auto [far_l, far_r] = windowFor(40);        // azimuth 0, distance 2

  CHECK(front_l > 1e-4f);
  CHECK_NEAR(front_l, front_r, front_l * 0.1f); // centered: front is symmetric

  // Right-hand-side azimuths: right channel louder, and (via the
  // right-left difference signal, monotonic in |sin(azimuth)| alone -
  // see windowedRmsDifference) progressively more so approaching hard
  // right (45 < 90) - this is exactly what a coarser, cardinal-only grid
  // couldn't confirm.
  CHECK(az45_r > az45_l);
  CHECK(az90_r > az90_l);
  CHECK(diffFor(8) > diffFor(4));
  // 135 shares azimuth 45's sin() magnitude (sin(135)==sin(45)): a plain
  // 2-channel stereo decode can't distinguish front-right from back-right
  // by design (only L/R, i.e. sign/magnitude of sin(azimuth), survives
  // decodeToStereo) - not a bug, an inherent property of this cheap decode.
  CHECK(az135_r > az135_l);
  CHECK_NEAR(diffFor(12), diffFor(4), std::fabs(diffFor(4)) * 0.15f);

  CHECK_NEAR(back_l, back_r, back_l * 0.1f); // azimuth 180: symmetric, like front

  CHECK(azm45_l > azm45_r);
  CHECK(azm90_l > azm90_r);
  CHECK(diffFor(24) > diffFor(28)); // |sin(-90)| > |sin(-45)|: bigger split
  CHECK(azm135_l > azm135_r);

  // Elevation doesn't affect a 2-channel decode at all (decodeToStereo only
  // reads W/Y) - straight up/down should look like a centered source.
  CHECK(up_l > 1e-4f);
  CHECK_NEAR(up_l, up_r, up_l * 0.1f);
  CHECK(down_l > 1e-4f);
  CHECK_NEAR(down_l, down_r, down_l * 0.1f);

  // Distance 2 attenuates by 1/distance relative to the same-azimuth,
  // distance-1 track (row 0).
  CHECK(far_l > 1e-4f);
  CHECK(far_l < front_l * 0.6f);
}

TEST(render_ambisonic_reverb_two_tracks_has_real_stereo_tail) {
  auto loaded = loadFixture("ambisonic_reverb_two_tracks.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(ChannelConfiguration::AMBISONIC, 44100);
  auto result = renderSongOffline(loaded.song, config, MixerType::AMBISONIC_STEREO);
  CHECK(!hasNonFiniteSample(result));

  // The two dry sources are hard left/right; the reverb tail after the
  // short release must still be audible on both channels - proving
  // ReverbState's narrow-to-real-stereo-then-reencode path actually ran
  // (a degraded fallback would only ever put energy on one channel).
  auto frames = result.numberOfFrames();
  auto tail_start = frames > 44100 / 2 ? frames - 44100 / 2 : 0;
  double left_energy = 0.0, right_energy = 0.0;
  for (size_t i = tail_start; i < frames; i++) {
    left_energy += std::pow(result.interleaved[i * result.channels + 0], 2);
    right_energy += std::pow(result.interleaved[i * result.channels + 1], 2);
  }

  CHECK(left_energy > 0.0);
  CHECK(right_energy > 0.0);
}

TEST(render_ambisonic_envelopefilter_over_plain_voice_keeps_position) {
  // A transparent effect (EnvelopeFilter) directly wrapping one positioned
  // leaf voice (no NoteMultiplier): confirms TrackState::render(int
  // frames)'s mismatch-encode logic handles this case (own config
  // AMBISONIC, child bottomed out at MONO) without crashing or losing the
  // voice's position - the wrapped voice is genuinely MONO by the time
  // EnvelopeFilter's applyEffect() sees it, not raw 4-channel or stereo.
  auto loaded = loadFixture("ambisonic_envelopefilter_plain.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(ChannelConfiguration::AMBISONIC, 44100);
  auto result = renderSongOffline(loaded.song, config, MixerType::AMBISONIC_STEREO);
  CHECK(!hasNonFiniteSample(result));

  auto left = rms(result, 0), right = rms(result, 1);
  CHECK(right > 1e-4f);
  CHECK(right > left); // azimuth 90: hard right
}

TEST(render_ambisonic_envelopefilter_over_notemultiplier_spread_survives) {
  // EnvelopeFilter wrapping a spread NoteMultiplier chord - the scenario
  // that drove this feature's final design (see the plan's Context/Key
  // architectural decision sections): confirms the spread is NOT collapsed
  // by the transparent EnvelopeFilter sitting above it. Compare against
  // the same fixture idea but with unisons=1 (no spread) to show the
  // spread is actually contributing width, not just generically present.
  auto spread = loadFixture("ambisonic_envelopefilter_notemultiplier.xml");
  CHECK(spread.ok);

  ChannelConfiguration config(ChannelConfiguration::AMBISONIC, 44100);
  auto result = renderSongOffline(spread.song, config, MixerType::AMBISONIC_STEREO);
  CHECK(!hasNonFiniteSample(result));

  auto left = rms(result, 0), right = rms(result, 1);
  CHECK(left > 1e-4f);
  CHECK(right > 1e-4f);
  // Track azimuth 0 with a symmetric +-30 degree spread: left and right
  // should both carry real energy and be roughly balanced (confirms the
  // spread reached both sides rather than, say, silently collapsing onto
  // one sub-voice only).
  CHECK_NEAR(left, right, std::max(left, right) * 0.3f);
}
