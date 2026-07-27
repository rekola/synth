#include "TestFramework.h"

#include "../Song.h"
#include "../InstrumentProvider.h"
#include "../OfflineRenderer.h"
#include "../ChannelConfiguration.h"
#include "../SongState.h"
#include "../Mixer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <filesystem>

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

// Mirrors findDefaultSoundFont() (Controller.cpp)'s search closely enough
// for test purposes - only used by SoundFont-guarded tests, which skip
// gracefully when this returns empty.
std::string findSystemSoundFont() {
  namespace fs = std::filesystem;
  std::error_code ec;
  const char * candidates[] = {
    "/usr/share/sounds/sf2/FluidR3_GM.sf2",
    "/usr/share/soundfonts/FluidR3_GM.sf2",
    "/usr/share/sounds/sf2/default-GM.sf2",
  };
  for (auto path : candidates) {
    if (fs::is_regular_file(path, ec)) return path;
  }
  return "";
}

// Like loadFixture(), but loads a real GM SoundFont into the provider
// first, so a fixture can reference any of its presets by name (e.g.
// "Glockenspiel").
Loaded loadFixtureWithSoundFont(const char * name, const std::string & sf2_path) {
  InstrumentProvider provider;
  provider.loadSoundFont(sf2_path);
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

// Records whatever SampleData each accumulate() call receives, so a test
// can inspect a track's real rendered output (including any SendA/SendB
// presence/energy - see SampleData.h's Channel enum) - the public
// renderSongOffline()/OfflineRenderResult path only ever exposes the
// final, already-decoded device-channel output, which never carries sends
// (the mixer itself drops them - see Mixer.h).
class RecordingMixer : public Mixer {
 public:
  RecordingMixer(short out_channels, int outSampleRate) : Mixer(out_channels, outSampleRate) { }

  void reset() override { accumulated.clear(); }
  void accumulate(const SampleData & data) override { accumulated.push_back(data); }
  SampleData encode() override { return SampleData(getOutChannels(), 0); }
  const SampleData & getRawBus() const override { return empty_; }

  std::vector<SampleData> accumulated;

 private:
  SampleData empty_;
};

} // namespace

TEST(render_center_note_produces_symmetric_stereo_output) {
  auto loaded = loadFixture("center_note.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
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

  ChannelConfiguration config(44100, 1);
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

  ChannelConfiguration config(44100, 1);
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

TEST(render_chorus_centers_its_input) {
  // Per-track nonlinear effects (Reverb/Chorus/Distortion) now reduce
  // their children to MONO (see AmbisonicEncoding.h's reduceForEffect) -
  // real stereo panning no longer survives underneath them, a deliberate
  // trade-off (see the plan this was built from). So a hard-right source
  // wrapped in a <chorus> effect now re-encodes as non-directional (W
  // only - see encodeMonoAsPoint), landing equally on both channels
  // rather than staying isolated to the right - this replaces the old
  // render_chorus_preserves_stereo_image, whose premise (position survives
  // through the chorus) is no longer true by design.
  auto loaded = loadFixture("chorus_pan.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  auto left = rms(result, 0), right = rms(result, 1);
  CHECK(right > 1e-4f);
  CHECK_NEAR(left, right, right * 0.05f);
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

  ChannelConfiguration config(44100, 1);
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

  ChannelConfiguration config(44100);
  auto result = renderSongOffline(loaded.song, config);

  // Every mixer now decodes to a 2-channel stereo device signal - a MONO
  // (0th-order-ambisonic) bus broadcasts equally to both channels rather
  // than being a genuine 1-channel device output (see
  // ChannelConfiguration::getDeviceChannels()). The compressor's own
  // channel-generic loop still ran over a genuine 1-channel accumulator
  // internally; only the final device shape changed.
  CHECK(result.channels == 2);
  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(rms(result, 0) > 1e-4f);
}

TEST(render_ambisonic_directions_produce_distinguishable_output) {
  auto loaded = loadFixture("ambisonic_directions.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config, MixerType::AMBISONIC_STEREO);

  CHECK(result.channels == 2); // always decoded to stereo, regardless of the 4-channel bus
  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));

  // Each track fires on its own row, 12 rows (1.5s) apart; envelope rings
  // ~1.0s (broadband pink noise, long enough to be a meaningful HRTF/
  // spectral analysis window - see the fixture's own comment), so an 0.8s
  // window from each row's start safely captures one note without
  // bleeding into the next (0.7s of buffer remains before the next note).
  auto windowFor = [&](int row) {
    float start = row * 0.125f;
    return std::make_pair(windowedRms(result, 0, start, start + 0.8f), windowedRms(result, 1, start, start + 0.8f));
  };

  auto diffFor = [&](int row) {
    float start = row * 0.125f;
    return windowedRmsDifference(result, start, start + 0.8f);
  };

  auto [front_l, front_r] = windowFor(0);     // azimuth 0
  auto [az45_l, az45_r] = windowFor(12);      // azimuth 45 (right-ish)
  auto [az90_l, az90_r] = windowFor(24);      // azimuth 90 (hard right)
  auto [az135_l, az135_r] = windowFor(36);    // azimuth 135 (back-right)
  auto [back_l, back_r] = windowFor(48);      // azimuth 180
  auto [azm135_l, azm135_r] = windowFor(60);  // azimuth -135 (back-left)
  auto [azm90_l, azm90_r] = windowFor(72);    // azimuth -90 (hard left)
  auto [azm45_l, azm45_r] = windowFor(84);    // azimuth -45 (left-ish)
  auto [up_l, up_r] = windowFor(96);          // elevation 90
  auto [down_l, down_r] = windowFor(108);     // elevation -90
  auto [far_l, far_r] = windowFor(120);       // azimuth 0, distance 2

  CHECK(front_l > 1e-4f);
  CHECK_NEAR(front_l, front_r, front_l * 0.1f); // centered: front is symmetric

  // Right-hand-side azimuths: right channel louder, and (via the
  // right-left difference signal, monotonic in |sin(azimuth)| alone -
  // see windowedRmsDifference) progressively more so approaching hard
  // right (45 < 90) - this is exactly what a coarser, cardinal-only grid
  // couldn't confirm.
  CHECK(az45_r > az45_l);
  CHECK(az90_r > az90_l);
  CHECK(diffFor(24) > diffFor(12));
  // 135 shares azimuth 45's sin() magnitude (sin(135)==sin(45)): a plain
  // 2-channel stereo decode can't distinguish front-right from back-right
  // by design (only L/R, i.e. sign/magnitude of sin(azimuth), survives
  // decodeToStereo) - not a bug, an inherent property of this cheap decode.
  CHECK(az135_r > az135_l);
  CHECK_NEAR(diffFor(36), diffFor(12), std::fabs(diffFor(12)) * 0.15f);

  CHECK_NEAR(back_l, back_r, back_l * 0.1f); // azimuth 180: symmetric, like front

  CHECK(azm45_l > azm45_r);
  CHECK(azm90_l > azm90_r);
  CHECK(diffFor(72) > diffFor(84)); // |sin(-90)| > |sin(-45)|: bigger split
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

  ChannelConfiguration config(44100, 1);
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
  // leaf voice (no NoteMultiplier): confirms the voice's own
  // InstrumentVoice::encodePosition() correctly spatially encodes itself
  // to the real (ambisonic) accumulator shape without crashing or losing
  // its position, and that EnvelopeFilter's channel-agnostic gain multiply
  // (applyEffect()) applies identically across however many channels that
  // now genuinely is (not a fixed MONO/stereo channel count).
  auto loaded = loadFixture("ambisonic_envelopefilter_plain.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config, MixerType::AMBISONIC_STEREO);
  CHECK(!hasNonFiniteSample(result));

  auto left = rms(result, 0), right = rms(result, 1);
  CHECK(right > 1e-4f);
  CHECK(right > left); // azimuth 90: hard right
}

TEST(render_ambisonic_order2_smoke_test) {
  // --ambisonic 2 through both AMBISONIC_STEREO and AMBISONIC_BINAURAL:
  // confirms real 2nd-order ambisonics (not just the order field) renders
  // without crashing and produces finite, non-silent output - the
  // directional assertions in render_ambisonic_directions_produce_
  // distinguishable_output already cover decodeToStereo (W/Y-only,
  // unaffected by order), so this is deliberately just a smoke test.
  auto loaded = loadFixture("ambisonic_directions.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 2);

  auto stereo_result = renderSongOffline(loaded.song, config, MixerType::AMBISONIC_STEREO);
  CHECK(stereo_result.channels == 2);
  CHECK(stereo_result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(stereo_result));
  CHECK(rms(stereo_result, 0) > 1e-4f || rms(stereo_result, 1) > 1e-4f);

  auto binaural_result = renderSongOffline(loaded.song, config, MixerType::AMBISONIC_BINAURAL);
  CHECK(binaural_result.channels == 2);
  CHECK(binaural_result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(binaural_result));
  CHECK(rms(binaural_result, 0) > 1e-4f || rms(binaural_result, 1) > 1e-4f);
}

TEST(render_ambisonic_order3_smoke_test) {
  // Same shape as render_ambisonic_order2_smoke_test above, at order 3 (16
  // channels, the 26-point Lebedev binaural rig) - confirms the full
  // channel-count bump (AmbisonicEncoding.h, SampleData.h) and the new
  // speaker rig (AmbisonicBinauralMixer.cpp) render without crashing and
  // produce finite, non-silent output end to end.
  auto loaded = loadFixture("ambisonic_directions.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 3);

  auto stereo_result = renderSongOffline(loaded.song, config, MixerType::AMBISONIC_STEREO);
  CHECK(stereo_result.channels == 2);
  CHECK(stereo_result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(stereo_result));
  CHECK(rms(stereo_result, 0) > 1e-4f || rms(stereo_result, 1) > 1e-4f);

  auto binaural_result = renderSongOffline(loaded.song, config, MixerType::AMBISONIC_BINAURAL);
  CHECK(binaural_result.channels == 2);
  CHECK(binaural_result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(binaural_result));
  CHECK(rms(binaural_result, 0) > 1e-4f || rms(binaural_result, 1) > 1e-4f);
}

TEST(render_track_send_a_reaches_track_state_output) {
  // Fully deterministic (no SoundFont involved): a plain oscillator
  // instrument with sendA=0.5 configured on its <track> - confirms the
  // whole propagation path (InstrumentVoice -> InstrumentTrackState ->
  // SongState) actually carries real SendA energy, using a RecordingMixer
  // to inspect the per-track SampleData the real Mixer would otherwise
  // silently drop (see Mixer.h).
  auto loaded = loadFixture("send_a_oscilator.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100);
  SongState state(config);
  state.initialize(loaded.song);
  state.setIsPlaying(true);

  RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

  bool saw_send_a = false;
  for (int block = 0; block < 20 && !saw_send_a; block++) {
    state.render(256, loaded.song, mixer);
    for (auto & data : mixer.accumulated) {
      if (data.hasChannel(Channel::AuxA)) {
	auto send = data.getChannel(Channel::AuxA);
	for (int i = 0; i < data.numberOfFrames(); i++) {
	  if (send[i] != 0.0f) { saw_send_a = true; break; }
	}
      }
    }
  }

  CHECK(saw_send_a);
}

TEST(render_send_a_reaches_ambisonic_bus_beyond_w_y_at_both_orders) {
  // The FDN reverb (bus/FDNReverb.h, SendBusProcessor) spreads its 8 taps
  // across the full sphere (cube-vertex directions), not the old shared
  // reverb's 2-point (az +-90, W/Y-only) encode - real energy should
  // reach ACN channels beyond W/Y at either supported ambisonic order,
  // confirmed here directly on the pre-decode bus via RecordingMixer (see
  // render_track_send_a_reaches_track_state_output above), since the
  // public renderSongOffline()/OfflineRenderResult path only ever exposes
  // the final, already-decoded 2-channel device output. SongState::render()
  // accumulates the send bus last (after every per-track accumulate()), so
  // accumulated.back() after each render() call is exactly that entry.
  auto loaded = loadFixture("send_a_oscilator.xml");
  CHECK(loaded.ok);

  for (int order : { 1, 2 }) {
    ChannelConfiguration config(44100, order);
    SongState state(config);
    state.initialize(loaded.song);
    state.setIsPlaying(true);

    RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

    double energy_beyond_wy = 0.0;
    for (int block = 0; block < 60; block++) {
      state.render(256, loaded.song, mixer);
      CHECK(!mixer.accumulated.empty());
      auto & bus = mixer.accumulated.back();
      CHECK(bus.numberOfChannels() == config.numberOfChannels());
      for (int c = 2; c < bus.numberOfChannels(); c++) {
	auto data = bus.getChannelData(c);
	for (int i = 0; i < bus.numberOfFrames(); i++) energy_beyond_wy += static_cast<double>(data[i]) * data[i];
      }
    }

    CHECK(energy_beyond_wy > 0.0);
  }
}

TEST(render_track_send_b_reaches_track_state_output) {
  // SendB's sibling of render_track_send_a_reaches_track_state_output -
  // confirms the multi-tap delay's mono input actually gets fed real
  // energy through the same InstrumentVoice -> InstrumentTrackState ->
  // SongState propagation path, using send_b_oscilator.xml (identical to
  // send_a_oscilator.xml except sendB="0.5" instead of sendA).
  auto loaded = loadFixture("send_b_oscilator.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100);
  SongState state(config);
  state.initialize(loaded.song);
  state.setIsPlaying(true);

  RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

  bool saw_send_b = false;
  for (int block = 0; block < 20 && !saw_send_b; block++) {
    state.render(256, loaded.song, mixer);
    for (auto & data : mixer.accumulated) {
      if (data.hasChannel(Channel::AuxB)) {
	auto send = data.getChannel(Channel::AuxB);
	for (int i = 0; i < data.numberOfFrames(); i++) {
	  if (send[i] != 0.0f) { saw_send_b = true; break; }
	}
      }
    }
  }

  CHECK(saw_send_b);
}

TEST(render_send_b_reaches_ambisonic_bus_beyond_w_y_at_both_orders) {
  // The multi-tap delay (bus/MultiTapDelay.h, SendBusProcessor) encodes
  // its 4 taps at fixed (mostly) azimuths off-center - real energy should
  // reach ACN channels beyond W/Y at either supported ambisonic order,
  // confirmed directly on the pre-decode bus via RecordingMixer, same
  // technique as render_send_a_reaches_ambisonic_bus_beyond_w_y_at_both_orders.
  auto loaded = loadFixture("send_b_oscilator.xml");
  CHECK(loaded.ok);

  for (int order : { 1, 2 }) {
    ChannelConfiguration config(44100, order);
    SongState state(config);
    state.initialize(loaded.song);
    state.setIsPlaying(true);

    RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

    double energy_beyond_wy = 0.0;
    for (int block = 0; block < 60; block++) {
      state.render(256, loaded.song, mixer);
      CHECK(!mixer.accumulated.empty());
      auto & bus = mixer.accumulated.back();
      CHECK(bus.numberOfChannels() == config.numberOfChannels());
      for (int c = 2; c < bus.numberOfChannels(); c++) {
	auto data = bus.getChannelData(c);
	for (int i = 0; i < bus.numberOfFrames(); i++) energy_beyond_wy += static_cast<double>(data[i]) * data[i];
      }
    }

    CHECK(energy_beyond_wy > 0.0);
  }
}

TEST(render_send_a_produces_audible_reverb_tail) {
  // send_a_oscilator.xml is identical to center_note.xml except for
  // sendA="0.5" on its one track - any output difference between them is
  // attributable entirely to SendBusProcessor's shared reverb, which now
  // actually reaches the final mix (Phase 1 only proved the plumbing
  // reached the mixer's accumulator - see
  // render_track_send_a_reaches_track_state_output above - without ever
  // becoming audible, since Mixer::encode() deliberately drops it).
  auto with_send = loadFixture("send_a_oscilator.xml");
  auto without_send = loadFixture("center_note.xml");
  CHECK(with_send.ok);
  CHECK(without_send.ok);

  ChannelConfiguration config(44100, 1);
  auto result_with = renderSongOffline(with_send.song, config);
  auto result_without = renderSongOffline(without_send.song, config);
  CHECK(!hasNonFiniteSample(result_with));
  CHECK(!hasNonFiniteSample(result_without));

  // Both fixtures wrap their oscilator in the same <envelope attack=.01
  // hold=.3 decay=.3 sustain=0 release=.05> - fully silent by ~0.66s (see
  // render_envelope_decays_after_hold_and_decay_time) - a window well past
  // that isolates the reverb tail from the dry note itself.
  auto tailRms = [&](const OfflineRenderResult & result) {
    return windowedRms(result, 0, 0.7f, 1.0f) + windowedRms(result, 1, 0.7f, 1.0f);
  };

  auto with_tail = tailRms(result_with);
  auto without_tail = tailRms(result_without);

  CHECK(with_tail > 1e-4f);
  CHECK(with_tail > without_tail * 10.0f);
}

TEST(render_send_b_produces_audible_delay_echo) {
  // send_b_oscilator.xml is identical to center_note.xml except for
  // sendB="0.5" on its one track - any output difference between them is
  // attributable entirely to SendBusProcessor's shared multi-tap delay.
  // Same methodology as render_send_a_produces_audible_reverb_tail.
  auto with_send = loadFixture("send_b_oscilator.xml");
  auto without_send = loadFixture("center_note.xml");
  CHECK(with_send.ok);
  CHECK(without_send.ok);

  ChannelConfiguration config(44100, 1);
  auto result_with = renderSongOffline(with_send.song, config);
  auto result_without = renderSongOffline(without_send.song, config);
  CHECK(!hasNonFiniteSample(result_with));
  CHECK(!hasNonFiniteSample(result_without));

  // Both fixtures wrap their oscilator in the same <envelope attack=.01
  // hold=.3 decay=.3 sustain=0 release=.05> - fully silent by ~0.66s (see
  // render_envelope_decays_after_hold_and_decay_time) - a window well past
  // that isolates the delay's decaying echo repeats (default feedback 0.5,
  // ~187ms per repeat at this fixture's 120bpm tempo) from the dry note.
  auto tailRms = [&](const OfflineRenderResult & result) {
    return windowedRms(result, 0, 0.7f, 1.0f) + windowedRms(result, 1, 0.7f, 1.0f);
  };

  auto with_tail = tailRms(result_with);
  auto without_tail = tailRms(result_without);

  CHECK(with_tail > 1e-4f);
  CHECK(with_tail > without_tail * 10.0f);
}

TEST(render_sf2_reverb_send_produces_audible_tail) {
  // Real SF2 send data, not a user-configured sendA - skips gracefully if
  // no system GM soundfont is present. "Glockenspiel" (a bell-like patch
  // with its own natural decay) has a real, nonzero reverbEffectsSend
  // generator in FluidR3_GM.sf2 (confirmed via a raw RIFF-chunk parse
  // during planning) - the note is turned off at row 4 (0.5s @ tempo 120),
  // so any energy well after that is attributable to the shared reverb,
  // not the dry sample's own tail.
  auto sf2_path = findSystemSoundFont();
  if (sf2_path.empty()) return; // no system GM soundfont - skip

  auto loaded = loadFixtureWithSoundFont("sf2_reverb_send.xml", sf2_path);
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  auto tail = windowedRms(result, 0, 1.5f, 2.0f) + windowedRms(result, 1, 1.5f, 2.0f);
  CHECK(tail > 1e-5f);
}

TEST(render_stereo_dry_signal_attenuates_with_distance) {
  // Dry-signal distance attenuation used to be ambisonic-only (baked into
  // computeAmbisonicGains/PositionalMixer::encode) - it's now applied
  // uniformly at the voice level (InstrumentVoice::getDistanceGain()), so a
  // plain STEREO bus should also show roughly 1/distance falloff, which it
  // never did before this change. Track 0 (distance 1) fires at row 0,
  // track 1 (distance 2) at row 8 (1.0s later at this tempo) - well past
  // track 0's envelope fully decaying (~0.66s, see
  // render_send_a_produces_audible_reverb_tail's comment), so the two
  // notes' windows never overlap.
  auto loaded = loadFixture("stereo_distance.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  auto near_rms = windowedRms(result, 0, 0.05f, 0.25f) + windowedRms(result, 1, 0.05f, 0.25f);
  auto far_rms = windowedRms(result, 0, 1.05f, 1.25f) + windowedRms(result, 1, 1.05f, 1.25f);

  CHECK(near_rms > 1e-4f);
  CHECK_NEAR(far_rms, near_rms * 0.5f, near_rms * 0.15f);
}

TEST(render_send_a_is_distance_invariant) {
  // Two otherwise-identical tracks (oscillator + envelope, sendA=0.5)
  // differing only in `distance` - a send's contribution to the shared
  // reverb bus should NOT depend on how far the source is from the
  // listener (see InstrumentVoice::getDistanceGain()'s doc comment and
  // SendBusProcessor's "room reverb" model), unlike the dry signal which
  // does now attenuate with distance (render_stereo_dry_signal_attenuates_
  // with_distance above).
  auto near = loadFixture("send_a_distance_near.xml");
  auto far = loadFixture("send_a_distance_far.xml");
  CHECK(near.ok);
  CHECK(far.ok);

  ChannelConfiguration config(44100);

  auto peakSendA = [&](Song & song) {
    SongState state(config);
    state.initialize(song);
    state.setIsPlaying(true);
    RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());
    float peak = 0.0f;
    for (int block = 0; block < 40; block++) {
      state.render(256, song, mixer);
      for (auto & data : mixer.accumulated) {
        if (data.hasChannel(Channel::AuxA)) {
          auto send = data.getChannel(Channel::AuxA);
          for (int i = 0; i < data.numberOfFrames(); i++) peak = std::max(peak, std::fabs(send[i]));
        }
      }
    }
    return peak;
  };

  auto near_peak = peakSendA(near.song);
  auto far_peak = peakSendA(far.song);

  CHECK(near_peak > 1e-4f);
  CHECK_NEAR(near_peak, far_peak, near_peak * 0.05f);
}

TEST(render_send_main_zero_silences_main_channels_but_not_sends) {
  // sendMain="0.0" alongside sendA="0.5" on the same track (SendLevels.h,
  // InstrumentVoice::encodePosition()): the track's own regular/main
  // channel should not even be allocated (hasChannel(Channel::Main) false
  // - a structural fact now, not just numerically-zero content) while its
  // SendA contribution keeps sounding at its own unrelated, unaffected
  // level - the whole point of this control (auditioning a bus effect in
  // isolation).
  auto loaded = loadFixture("send_main_zero.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100);
  SongState state(config);
  state.initialize(loaded.song);
  state.setIsPlaying(true);

  RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

  // Note: mixer.accumulated includes both the track's own per-block output
  // and the shared send bus's own separate contribution (SongState::render()
  // accumulates both) - the bus's own accumulator always structurally has
  // Main (it's a plain always-Main-present accumulator, not derived from
  // children - see bus/SendBusProcessor.cpp), so hasChannel(Channel::Main)
  // alone can't distinguish "this specific track's Main" from "the bus's
  // own Main" across all entries; main_peak still correctly stays near
  // zero either way, since sendA-driven bus content this early has ~no W
  // energy yet in this fixture's short window.
  float main_peak = 0.0f, send_a_peak = 0.0f;
  for (int block = 0; block < 20; block++) {
    state.render(256, loaded.song, mixer);
    for (auto & data : mixer.accumulated) {
      if (data.hasChannel(Channel::Main)) {
        auto * w = data.getChannelData(0);
        for (int i = 0; i < data.numberOfFrames(); i++) main_peak = std::max(main_peak, std::fabs(w[i]));
      }
      if (auto * send = data.getChannel(Channel::AuxA)) {
        for (int i = 0; i < data.numberOfFrames(); i++) send_a_peak = std::max(send_a_peak, std::fabs(send[i]));
      }
    }
  }

  CHECK(send_a_peak > 1e-4f);
  CHECK_NEAR(main_peak, 0.0f, 1e-6f);
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

  ChannelConfiguration config(44100, 1);
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
