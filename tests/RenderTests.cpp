#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/model/SampleTrack.h"
#include "../src/model/Clip.h"
#include "../src/model/SampleContent.h"
#include "../src/model/ArrangementOps.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/audio/OfflineRenderer.h"
#include "../src/audio/AudioBuffer.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/state/SongState.h"
#include "../src/state/InstrumentTrackState.h"
#include "../src/state/NoteOrigin.h"
#include "../src/ambisonic/Mixer.h"
#include "../src/ambisonic/MixerFactory.h"
#include "../src/ambisonic/MixerType.h"
#include "../src/dsp/DiracAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
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
  InstrumentProvider provider; // no SoundFont: fixtures only use built-in oscillators
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
  for (size_t i = 0; i < frames; i++) sum += static_cast<double>(std::pow(result.interleaved[i * static_cast<size_t>(result.channels) + static_cast<size_t>(channel)], 2));
  return frames ? static_cast<float>(std::sqrt(sum / frames)) : 0.0f;
}

float windowedRms(const OfflineRenderResult & result, int channel, float start_s, float end_s) {
  auto frames = result.numberOfFrames();
  size_t start = std::min<size_t>(static_cast<size_t>(start_s * result.sampleRate), frames);
  size_t end = std::min<size_t>(static_cast<size_t>(end_s * result.sampleRate), frames);
  if (end <= start) return 0.0f;
  double sum = 0.0;
  for (size_t i = start; i < end; i++) sum += std::pow(result.interleaved[i * static_cast<size_t>(result.channels) + static_cast<size_t>(channel)], 2);
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
    auto diff = result.interleaved[i * static_cast<size_t>(result.channels) + 1] - result.interleaved[i * static_cast<size_t>(result.channels) + 0];
    sum += std::pow(diff, 2);
  }
  return static_cast<float>(std::sqrt(sum / (end - start)));
}

// A plain zero-crossing-rate pitch estimate over [start_s, end_s) - not a
// real pitch detector, just enough to tell "this window sounds like a
// higher/lower tone than that one" apart for a pure oscillator tone, the
// same technique tests/ArpeggiatorStateTests.cpp's own octave-widening
// test uses directly on an ArpeggiatorState's output.
float windowedZeroCrossingRate(const OfflineRenderResult & result, int channel, float start_s, float end_s) {
  auto frames = result.numberOfFrames();
  size_t start = std::min<size_t>(static_cast<size_t>(start_s * result.sampleRate), frames);
  size_t end = std::min<size_t>(static_cast<size_t>(end_s * result.sampleRate), frames);
  if (end <= start + 1) return 0.0f;
  int crossings = 0;
  for (size_t i = start + 1; i < end; i++) {
    auto prev = result.interleaved[(i - 1) * static_cast<size_t>(result.channels) + static_cast<size_t>(channel)];
    auto cur = result.interleaved[i * static_cast<size_t>(result.channels) + static_cast<size_t>(channel)];
    if ((prev < 0.0f) != (cur < 0.0f)) crossings++;
  }
  return static_cast<float>(crossings) / static_cast<float>(end - start);
}

bool hasNonFiniteSample(const OfflineRenderResult & result) {
  for (auto v : result.interleaved) {
    if (!std::isfinite(v)) return true;
  }
  return false;
}

// FNV-1a-64 over every output sample's raw bit pattern - used only by the
// golden-render regression tests below, to catch a change in rendered
// output that wouldn't necessarily show up as non-finite or wildly
// out-of-range (a broken HashField coordinate, an accidentally reordered
// draw, a reintroduced rand() call). Not a general-purpose hash and not
// HashField's own hash64() - this runs over an arbitrary-length byte
// stream, a different shape of problem from HashField's fixed (coord,
// param, salt) inputs.
uint64_t hashSamples(const OfflineRenderResult & result) {
  uint64_t h = 14695981039346656037ull; // FNV-1a 64-bit offset basis
  for (float v : result.interleaved) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int shift = 0; shift < 32; shift += 8) {
      h ^= static_cast<uint8_t>(bits >> shift);
      h *= 1099511628211ull; // FNV-1a 64-bit prime
    }
  }
  return h;
}

// Records whatever AudioBuffer each accumulate() call receives, so a test
// can inspect a track's real rendered output (including any SendA/SendB
// presence/energy - see AudioBuffer.h's Channel enum) - the public
// renderSongOffline()/OfflineRenderResult path only ever exposes the
// final, already-decoded device-channel output, which never carries sends
// (the mixer itself drops them - see Mixer.h).
class RecordingMixer : public Mixer {
 public:
  RecordingMixer(short out_channels, int outSampleRate) : Mixer(out_channels, outSampleRate) { }

  void reset() override { accumulated.clear(); }
  void accumulate(const AudioBuffer & data) override { accumulated.push_back(data); }
  AudioBuffer encode() override { return AudioBuffer(getOutChannels(), 0); }
  const AudioBuffer & getRawBus() const override { return empty_; }

  std::vector<AudioBuffer> accumulated;

 private:
  AudioBuffer empty_;
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

// The arrangement layer's own instance events actually drive real
// (transport) playback, not just PatternEditor's Ctrl-K/Session view's
// live triggering - SongState::renderBlock()'s note scheduler.

// An active instance masks whatever the background has underneath it,
// even when the instance's own content there is empty/rest - the clip
// placed here has no notes at all, so if the background's own note at
// the same row still sounded, the instance wouldn't really be masking
// anything.
TEST(render_active_instance_suppresses_the_background_underneath) {
  auto loaded = loadFixture("arrangement_instance_suppresses_background.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(rms(result, 0) < 1e-4f);
}

// The other half of the same guarantee: an active instance's own content
// actually gets scheduled and heard, not just the suppression above -
// this fixture's background has no note at all, only the clip does.
TEST(render_active_instance_plays_its_own_content) {
  auto loaded = loadFixture("arrangement_instance_plays_its_own_content.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(rms(result, 0) > 1e-4f);
}

// SampleTrack's own clip-instance path: SongState.h's clip-transition
// detection calls SampleTrackState::triggerClip() directly (never
// noteOn()/getLeafPattern()), so this exercises a genuinely different
// code path from the note-based instance tests just above, not just a
// different fixture. Fixture: a one-shot (loop="0") clip referencing an
// 0.05s, 8kHz sidecar tone triggered at row 0, tempo 120.
TEST(render_sample_track_clip_instance_plays_its_own_audio) {
  auto loaded = loadFixture("sample_track_clip.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(8000); // matches the fixture .wav's own native rate - no resampling
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(windowedRms(result, 0, 0.0f, 0.04f) > 1e-3f); // the tone is sounding
  CHECK(windowedRms(result, 0, 0.5f, 1.0f) < 1e-4f); // one-shot - silent well after its own 0.05s length, not looping
}

// A real bug report: a one-shot clip's own real audio can outlast the
// section it's placed in - SongState.h's own transition-detection stop
// only fires once something *else* is found at this track's position,
// which never happens on a single section that just loops back to itself
// and keeps re-finding this same, unchanged instance (never re-triggered,
// since active.clip_index never actually changes). SongState.h instead
// queues an explicit RenderContext::addPendingSampleStop() at the exact
// frame the section's own last row ends, applied by SampleTrackState::
// render()'s own chunked loop as a short natural release, independent of
// whether any transition is ever detected at all. Fixture: rowsPerBar
// 4, tempo 120 (row duration 0.125s), a single 1-bar (4-row, 0.5s) section,
// a one-shot clip referencing a 2s sidecar tone triggered at row 0 - the
// real audio is 4x longer than the section it's placed in.
TEST(render_sample_track_clip_stops_at_the_section_boundary_even_when_its_own_audio_outlasts_it) {
  auto loaded = loadFixture("sample_track_clip_outlasts_scene.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(8000); // matches the fixture .wav's own native rate - no resampling
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(windowedRms(result, 0, 0.1f, 0.3f) > 1e-3f); // sounding, well within the section's own 0.5s
  CHECK(windowedRms(result, 0, 1.0f, 1.9f) < 1e-4f); // silent well past the section boundary, even though the real audio (2s) would otherwise still be sounding here
}

// A real design decision, raised directly by the user: a looping
// SampleTrack clip's own lap length is the clip's own length
// (Clip::getLength()), not its real audio duration - the intended use is
// a short, atmospheric/one-shot-feeling sound authored inside a longer
// clip slot, finishing on its own and sitting in silence for the rest of
// the lap before the *clip* restarts (not the sample). Realized entirely
// by SongState.h's own per-row scheduling re-triggering a fresh voice
// each lap (mirroring LaunchpadManager::fireOrTriggerClipStep(), which
// already worked this way for Session-view triggering) - SampleClipVoice
// itself has no looping concept of its own any more. Fixture: rowsPerBar
// 4, tempo 120 (row duration 0.125s), a looping 4-row (0.5s) clip
// referencing a 0.2s sidecar tone, in a 2-bar (8-row, 1.0s) section - two
// full laps fit, each with 0.3s of real silence at its own end.
TEST(render_sample_track_loop_shorter_than_the_clip_goes_silent_then_restarts_each_lap) {
  auto loaded = loadFixture("sample_track_loop_shorter_than_clip.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(8000); // matches the fixture .wav's own native rate - no resampling
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(windowedRms(result, 0, 0.0f, 0.15f) > 1e-3f); // first lap - the audio, playing
  CHECK(windowedRms(result, 0, 0.3f, 0.45f) < 1e-4f); // first lap's own silent tail - the audio already finished, but the lap hasn't
  CHECK(windowedRms(result, 0, 0.5f, 0.65f) > 1e-3f); // second lap - re-triggered, sounding again, not just silence forever
  CHECK(windowedRms(result, 0, 0.8f, 0.95f) < 1e-4f); // and its own silent tail too
}

// The real-song-pipeline counterpart to SampleTrackTests.cpp's own
// triggerClip()-level stretch tests: confirms the whole path (XML's
// originalTempo attribute -> SongState.h's scheduling -> RenderContext ->
// SampleTrackState::triggerClip() -> TimeStretcher) actually reaches a
// stretched voice, not just the unit-level pieces in isolation. Fixture:
// tempo 60, a 2s/8kHz sidecar tone recorded at originalTempo 120 (half the
// song's own tempo -> ratio 0.5, roughly double duration, ~4s), one-shot,
// in a 6-bar (6.0s) section comfortably longer than the stretched result.
TEST(render_sample_track_clip_stretches_to_match_a_disagreeing_song_tempo) {
  auto loaded = loadFixture("sample_track_clip_needs_stretching.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(8000); // matches the fixture .wav's own native rate - no resampling
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(windowedRms(result, 0, 0.1f, 0.3f) > 1e-3f); // sounding early on
  CHECK(windowedRms(result, 0, 2.5f, 2.7f) > 1e-3f); // still sounding well past the *unstretched* 2s length - only real stretching explains this
  CHECK(windowedRms(result, 0, 5.0f, 5.5f) < 1e-4f); // silent well before the section's own 6s end - genuinely finished, not just clipped by the section boundary
}

// A SampleTrack's own background bed (Section::getSampleBackgroundContent(),
// written by ArrangementOps.h's mergeClipToBackground()) actually plays
// during real (transport) playback, not just the model-layer coverage
// ArrangementOpsTests.cpp already has for the mix itself. Specifically
// exercises the leftover "OFF" instance mergeClipToBackground() places at
// the row it just freed: the bed must stay audible right through it - a
// stop only ever silences a real clip, never the baked bed underneath it
// (SongState.h's own comment on why this differs from a note track's own
// background Pattern, which a stop still silences). rowsPerBar 4, tempo
// 120 (interval 1000 frames @ 8kHz), an 8-row (2-bar) section.
TEST(render_sample_track_background_bed_plays_through_the_merges_own_leftover_stop) {
  Song song;
  song.setTempo(120);
  song.setRowsPerBar(4);
  auto & track = song.addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  ChannelConfiguration config(8000); // no resampling - native rate matches
  auto interval = config.getSampleInterval(song.getTempo());
  CHECK(interval == 1000);

  Clip source(track_id);
  auto & content = source.getOrCreateSampleContent();
  auto buffer = std::make_shared<AudioBuffer>(1, 8 * interval); // fills the whole section
  auto data = buffer->getChannelData(0);
  for (int i = 0; i < 8 * interval; i++) data[i] = 0.3f;
  content.setBuffer(buffer);
  content.setNativeSampleRate(8000);
  source.setLength(8);
  source.setLooping(false);
  song.addClip(std::move(source)); // index 0

  auto & section = song.addSection();
  section.setLengthBars(2); // 8 rows @ rowsPerBar 4
  placeClipInstance(song, section, track_id, 0, 0);
  CHECK(mergeClipToBackground(song, section, track_id, 0, config) == true);
  // The merge's own cleanup leaves an explicit stop at row 0, not a real
  // instance - resolveInstanceAt() must therefore report it, distinctly
  // from "nothing was ever placed," so this test is actually exercising
  // the fix and not a no-op case.
  CHECK(resolveInstanceAt(song, section, track_id, 0).clip_index == Section::kStopInstance);

  auto result = renderSongOffline(song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));
  CHECK(windowedRms(result, 0, 0.0f, 0.4f) > 1e-3f); // the bed, audible right through the merge's own leftover stop
}

// The other half of the same guarantee, and the reason the bed and a real
// clip are two independent, simultaneously-active voices rather than one
// masking the other (SampleTrackState's own doc comment): a clip placed
// on top of an already-merged bed must genuinely mix with it, not replace
// it - real audio has a well-defined "sum," unlike two Notes. Same section
// shape as above; a second, un-merged clip (a distinct tone) placed at
// rows 4-5 on top of the already-merged bed. Distinguishes real mixing
// from masking by comparing the *ratio* against the bed's own alone
// level: masking (overlay's 0.9 alone) would read ~4.5x the bed-alone
// level (0.2); real mixing (0.2+0.9=1.1) reads ~5.5x - the two hypotheses
// are close enough that only an exact combined-amplitude check tells them
// apart, not just "louder than before."
TEST(render_sample_track_background_bed_mixes_with_a_real_clip_on_top) {
  Song song;
  song.setTempo(120);
  song.setRowsPerBar(4);
  auto & track = song.addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  ChannelConfiguration config(8000);
  auto interval = config.getSampleInterval(song.getTempo());
  CHECK(interval == 1000);

  Clip source(track_id);
  auto & content = source.getOrCreateSampleContent();
  auto buffer = std::make_shared<AudioBuffer>(1, 8 * interval);
  auto data = buffer->getChannelData(0);
  for (int i = 0; i < 8 * interval; i++) data[i] = 0.2f;
  content.setBuffer(buffer);
  content.setNativeSampleRate(8000);
  source.setLength(8);
  source.setLooping(false);
  song.addClip(std::move(source)); // index 0

  auto & section = song.addSection();
  section.setLengthBars(2); // 8 rows
  placeClipInstance(song, section, track_id, 0, 0);
  CHECK(mergeClipToBackground(song, section, track_id, 0, config) == true); // bed now fills the whole section at 0.2

  Clip overlay(track_id);
  auto & overlay_content = overlay.getOrCreateSampleContent();
  auto overlay_buffer = std::make_shared<AudioBuffer>(1, 2 * interval); // exactly rows 4-5's own span
  auto overlay_data = overlay_buffer->getChannelData(0);
  for (int i = 0; i < 2 * interval; i++) overlay_data[i] = 0.9f;
  overlay_content.setBuffer(overlay_buffer);
  overlay_content.setNativeSampleRate(8000);
  overlay.setLength(2);
  overlay.setLooping(false);
  song.addClip(std::move(overlay)); // index 1, never merged - stays a live instance
  placeClipInstance(song, section, track_id, 4, 1);

  auto result = renderSongOffline(song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));

  // Windows kept comfortably inside each sustained region, away from any
  // 10ms release tail (the clip layer's own transition-out fade, e.g. at
  // the overlay's own row-6 expiry) - the bed's own voice is never
  // touched by any of that (it's an entirely separate, independent
  // voice), so it has no edges of its own to avoid here at all.
  auto rms_bed_alone = windowedRms(result, 0, 0.1f, 0.35f);    // rows 0-2ish: the bed alone, amplitude 0.2
  auto rms_combined = windowedRms(result, 0, 0.51f, 0.54f);    // rows 4-5: the bed (0.2) and the overlay clip (0.9) together
  auto rms_bed_resumed = windowedRms(result, 0, 0.76f, 0.94f); // rows 6-7: the bed alone again, once the overlay's own placement ends

  CHECK(rms_bed_alone > 1e-3f);
  CHECK(rms_combined > 1e-3f);
  CHECK(rms_bed_resumed > 1e-3f);

  auto ratio = rms_combined / rms_bed_alone; // true additive mix: ~(0.2+0.9)/0.2 = 5.5; masking (overlay alone): ~0.9/0.2 = 4.5
  CHECK(ratio > 5.0f); // clearly above the "overlay masks the bed" hypothesis - the bed's own energy is still there too
  CHECK(ratio < 6.0f); // and not implausibly higher either
  CHECK_NEAR(rms_bed_resumed, rms_bed_alone, rms_bed_alone * 0.1f); // the bed resumed at exactly its own original level, not something else
}

// Positioning the playhead mid-instance while stopped, then pressing play,
// must start the clip's own audio from the matching offset - not from its
// own beginning, and not silently do nothing because this SongState's own
// bookkeeping already thought that instance was active from before the
// transport stopped. Drives SongState::renderBlock()/setIsPlaying()/
// setPosition() directly (ResyncPlayheadTests.cpp's own precedent for
// exercising a stop/resume cycle), not through renderSongOffline() - the
// pipeline always starts at absolute row 0 and never stops, so it can't
// reach this case. Fixture buffer: rowsPerBar 4, tempo 120 (row duration
// 0.125s, interval 1000 frames @ 8kHz) - silent for its own first 5 rows,
// then a constant tone for the rest, in an 8-row one-shot clip triggered
// at row 0.
TEST(render_sample_track_resumes_a_stopped_instance_from_the_row_the_playhead_landed_on) {
  Song song;
  song.setTempo(120);
  song.setRowsPerBar(4);
  auto & track = song.addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  ChannelConfiguration config(8000); // no resampling - native rate matches
  auto interval = config.getSampleInterval(song.getTempo());
  CHECK(interval == 1000);

  Clip clip(track_id);
  auto & content = clip.getOrCreateSampleContent();
  auto buffer = std::make_shared<AudioBuffer>(1, 8 * interval);
  auto data = buffer->getChannelData(0);
  for (int i = 0; i < 8 * interval; i++) data[i] = i < 5 * interval ? 0.0f : 0.5f; // silent, then a tone from row 5 onward
  content.setBuffer(buffer);
  content.setNativeSampleRate(8000);
  clip.setLength(8);
  clip.setLooping(false);
  song.addClip(std::move(clip)); // index 0

  auto & section = song.addSection();
  section.setLengthBars(2); // 8 rows @ rowsPerBar 4
  placeClipInstance(song, section, track_id, 0, 0);

  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);

  state.setIsPlaying(true);
  state.renderBlock(3 * interval, song, *mixer); // lands at row 3, still in the silent region
  auto through_row_3 = mixer->encode();
  for (int i = 0; i < through_row_3.numberOfFrames(); i++) CHECK(through_row_3.getChannelData(0)[i] == 0.0f);

  // Stopped - a single 1-frame renderBlock() call here is what actually
  // lets SongState notice the transport stopped at all (isPlaying() is
  // only ever compared against its own *previous* renderBlock() call, and
  // the real audio thread keeps calling this every block regardless of
  // isPlaying() - see renderBlock()'s own comment). This also queues the
  // pause-release (render_sample_track_pausing_releases_the_sounding_voice_
  // instead_of_leaving_it_ringing below has that on its own), but there's
  // nothing to release here yet - the voice sat frozen in the silent
  // region (row 3) the whole time, this call included, since nothing was
  // ever actually rendered at row 5 onward to reach the tone. The playhead
  // is then moved elsewhere entirely (row 6, deep in the tone - neither
  // where playback stopped, row 3, nor the instance's own leading row, 0)
  // - the same "position the playhead on any row" gesture PatternEditor's
  // own cursor navigation performs while stopped.
  state.setIsPlaying(false);
  state.renderBlock(1, song, *mixer);
  state.setPosition(6);

  state.setIsPlaying(true);
  state.renderBlock(10, song, *mixer);
  auto after_resume = mixer->encode();
  for (int i = 0; i < after_resume.numberOfFrames(); i++) CHECK(after_resume.getChannelData(0)[i] != 0.0f);
}

// Pausing mid-clip must release the sounding voice (a short natural fade,
// same as SampleClipVoice's own 10ms release everywhere else) rather than
// either leaving it ringing straight through the pause (a SampleTrack
// voice has no pause-awareness of its own - only start/stop) or cutting
// it off hard (a click). Fixture: a one-shot 8-row clip, its own real
// audio a full 8 rows/1s long, stopped 3 rows in - well before its own
// natural end, so anything heard fading out afterward is genuinely the
// pause-release firing, not the clip simply finishing on its own.
TEST(render_sample_track_pausing_releases_the_sounding_voice_instead_of_leaving_it_ringing) {
  Song song;
  song.setTempo(120);
  song.setRowsPerBar(4);
  auto & track = song.addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  ChannelConfiguration config(8000); // no resampling - native rate matches
  auto interval = config.getSampleInterval(song.getTempo());
  CHECK(interval == 1000);

  Clip clip(track_id);
  auto & content = clip.getOrCreateSampleContent();
  auto buffer = std::make_shared<AudioBuffer>(1, 8 * interval);
  auto data = buffer->getChannelData(0);
  for (int i = 0; i < 8 * interval; i++) data[i] = 0.5f; // a constant tone the whole way through
  content.setBuffer(buffer);
  content.setNativeSampleRate(8000);
  clip.setLength(8);
  clip.setLooping(false);
  song.addClip(std::move(clip)); // index 0

  auto & section = song.addSection();
  section.setLengthBars(2); // 8 rows @ rowsPerBar 4
  placeClipInstance(song, section, track_id, 0, 0);

  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);

  state.setIsPlaying(true);
  state.renderBlock(3 * interval, song, *mixer); // 3 rows in, comfortably still within the clip's own 8-row content
  auto through_row_3 = mixer->encode();
  CHECK(through_row_3.getChannelData(0)[through_row_3.numberOfFrames() - 1] != 0.0f); // still sounding right up to the pause

  // Stopped - the very next block is where the release actually happens
  // (queued at frame 0 of it, see renderBlock()'s own comment), rendered
  // in full here (150 frames - comfortably more than the 10ms/80-frame
  // release) so both ends of the fade are visible in one call.
  state.setIsPlaying(false);
  state.renderBlock(150, song, *mixer);
  auto released = mixer->encode();
  auto out = released.getChannelData(0);

  CHECK(out[0] != 0.0f); // no hard cut - still audible the instant pause takes effect
  for (int i = 100; i < released.numberOfFrames(); i++) CHECK(out[i] == 0.0f); // fully released well before the block ends, not left ringing
}

// A real bug report: resuming sometimes played nothing at all. Root cause:
// the resume-retrigger flag was a plain local, recomputed fresh from
// was_playing_ every renderBlock() call, rather than a persistent
// obligation - stopping and resuming *mid-row* (sample_pos_ left wherever
// it was, never reset, since neither setPosition() nor movePosition() was
// called - a plain pause/resume in place) meant the very first resumed
// call could easily end before ever reaching a row boundary, especially
// against a real-time engine's own small period size; by the next call,
// was_playing_ had already latched true, so the flag recomputed false
// again with nothing having ever consumed it - silently losing the
// obligation to retrigger at all. Reproduced here by driving many small,
// non-row-aligned blocks (64 frames - a realistic ALSA period, well short
// of this fixture's own 1000-frame row interval) through the resume,
// mirroring the real audio thread's own call pattern rather than one
// large block that would paper over the bug.
TEST(render_sample_track_resuming_mid_row_across_several_small_blocks_still_plays) {
  Song song;
  song.setTempo(120);
  song.setRowsPerBar(4);
  auto & track = song.addTrack(std::make_unique<SampleTrack>());
  auto track_id = track.getInternalId();

  ChannelConfiguration config(8000); // no resampling - native rate matches
  auto interval = config.getSampleInterval(song.getTempo());
  CHECK(interval == 1000);

  Clip clip(track_id);
  auto & content = clip.getOrCreateSampleContent();
  auto buffer = std::make_shared<AudioBuffer>(1, 8 * interval);
  auto data = buffer->getChannelData(0);
  for (int i = 0; i < 8 * interval; i++) data[i] = 0.5f; // a constant tone the whole way through
  content.setBuffer(buffer);
  content.setNativeSampleRate(8000);
  clip.setLength(8);
  clip.setLooping(false);
  song.addClip(std::move(clip)); // index 0

  auto & section = song.addSection();
  section.setLengthBars(2); // 8 rows @ rowsPerBar 4
  placeClipInstance(song, section, track_id, 0, 0);

  auto mixer = createMixer(config, MixerType::AMBISONIC_STEREO);
  SongState state(config);
  state.initialize(song);

  state.setIsPlaying(true);
  state.renderBlock(3 * interval + 500, song, *mixer); // 3.5 rows in - deliberately mid-row
  // Not pinned to an exact value: a single renderBlock() call spanning
  // several row transitions drifts sample_pos_ by a little (a separate,
  // pre-existing quirk in the row-advance loop's own bookkeeping, not
  // this test's concern) - only "genuinely mid-row, not sitting on a
  // boundary" actually matters for exercising the bug below.
  CHECK(state.getSamplePos() != 0);

  // Stopped in place (no setPosition()/movePosition() call) - sample_pos_
  // stays exactly where it was, still mid-row. Rendered for 100 frames
  // (comfortably more than the pause-release's own 80-frame length, not
  // just enough to let SongState notice the stop) so the release from
  // pausing has fully finished by the time the resume loop below starts -
  // otherwise its own tail bleeding into that loop's first block would
  // make heard_sound below true regardless of whether the retrigger this
  // test actually cares about ever fires at all.
  state.setIsPlaying(false);
  state.renderBlock(100, song, *mixer);
  CHECK(state.getSamplePos() != 0); // confirms nothing moved it while stopped

  // Resumed in place too - still short of the next row boundary. Driven
  // through several 64-frame blocks (real ALSA-period sized, not one
  // large block), none of which individually reaches that boundary until
  // partway through the run.
  state.setIsPlaying(true);
  bool heard_sound = false;
  for (int block = 0; block < 12; block++) { // 12 * 64 = 768 frames, comfortably past the 500 remaining
    state.renderBlock(64, song, *mixer);
    auto rendered = mixer->encode();
    auto out = rendered.getChannelData(0);
    for (int i = 0; i < rendered.numberOfFrames(); i++) {
      if (out[i] != 0.0f) heard_sound = true;
    }
  }
  CHECK(heard_sound); // the retrigger must still fire once the row boundary is finally reached, not get lost along the way
}

// An explicit stop instance (Section::kStopInstance, ArrangementOps.h's own
// placeStopInstance()) placed after a *looping* clip's own trigger must
// actually silence it going forward - SongState::renderBlock()'s own
// termination-detection now fires the instrument's natural release
// (InstrumentTrackState::stopAllVoices()) the moment resolveInstanceAt()
// stops returning that clip, rather than only stopping new note-ONs and
// leaving an already-sustaining voice (sustain=1.0 here, so it never ends
// on its own) ringing indefinitely. This is what LaunchpadManager::
// placeRecordingStop() (Session-view "stop this track" - an empty-row
// press or CC49) relies on to actually be heard, not just recorded.
TEST(render_stop_instance_silences_a_looping_instance_going_forward) {
  auto loaded = loadFixture("arrangement_stop_silences_a_looping_instance.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);

  CHECK(result.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result));

  // Fixture: rowsPerBar=4, tempo=120 (row_duration=0.125s), a looping
  // 4-row clip starts at row 0, an explicit stop lands at row 8 (1.0s in).
  CHECK(windowedRms(result, 0, 0.1f, 0.9f) > 1e-3f); // looping and audible before the stop
  CHECK(windowedRms(result, 0, 2.0f, 3.9f) < 1e-4f); // silenced well past the stop, not just decayed - the section keeps running (background/pattern-length wise) all the way to row 32 (4.0s), so this alone rules out "it just ran out of song"
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

// Solo enforcement across independent top-level tracks depends on
// SongState::renderBlock() actually summing them through the master
// track's own TrackState::render() (renderChildren()'s solo-aware mix,
// same mechanism a Group/wrapping Effect's own children already use) -
// bypassing that (accumulating each top-level track into the mixer
// directly, unconditionally) would silently ignore solo entirely for
// tracks that aren't siblings under some shared non-root parent.
TEST(render_solo_on_one_top_level_track_silences_another) {
  // Comparing against a reference render where track 1 never existed at
  // all, rather than asserting the "silenced" side's own RMS is near
  // zero - decodeToStereo's cheap cardioid matrix doesn't null out a hard
  // -panned source's quiet side, it phase-inverts it at similar magnitude
  // (see windowedRmsDifference()'s own comment above), so a plain
  // near-zero check on one channel is not a valid way to tell "silenced"
  // from "just panned" apart.
  auto soloed = loadFixture("solo_silences_other_tracks.xml");
  auto alone = loadFixture("solo_reference_track0_alone.xml");
  CHECK(soloed.ok);
  CHECK(alone.ok);

  ChannelConfiguration config(44100, 1);
  auto result_soloed = renderSongOffline(soloed.song, config);
  auto result_alone = renderSongOffline(alone.song, config);

  CHECK(result_soloed.numberOfFrames() > 0);
  CHECK(!hasNonFiniteSample(result_soloed));

  auto left_soloed = rms(result_soloed, 0), right_soloed = rms(result_soloed, 1);
  auto left_alone = rms(result_alone, 0), right_alone = rms(result_alone, 1);

  CHECK(left_soloed > 1e-4f);
  CHECK_NEAR(left_soloed, left_alone, left_alone * 0.05f);
  CHECK_NEAR(right_soloed, right_alone, std::max(right_alone * 0.05f, 1e-4f));
}

// Reproduction for a reported regression, live-audition flavor: type a
// note into a PercussionTrack's column (PatternEditor auditions it
// immediately, same as InstrumentTrackState::noteOn() below), then type
// OFF on the same column while still stopped (PatternEditor's own is_off
// branch, same as noteOff() below) - the live voice should stop, exactly
// like the already-passing instrument-track equivalent
// (live_note_off_reclaims_the_voice, PlayerMultiBufferTests.cpp) - this
// is the same mechanism through a real PercussionTrackState instead of a
// plain InstrumentTrackState, never exercised directly until now.
TEST(render_percussion_live_note_off_reclaims_the_voice) {
  auto loaded = loadFixture("percussion_note_off.xml");
  CHECK(loaded.ok);
  auto & song = loaded.song;

  auto & percussion_track = *song.getMasterTrack().getChildren()[0];

  ChannelConfiguration config(44100, 1);
  SongState state(config);
  state.initialize(song);
  // Deliberately not setIsPlaying(true) - this is edit-time audition
  // while stopped, not pattern-driven playback (that's the other test
  // just below, which already passes).

  auto & track_state = dynamic_cast<InstrumentTrackState &>(percussion_track.getState(state, state.getSongStructure()));
  auto instrument = track_state.getInstrumentSource(song.getInstrumentPool());
  CHECK(instrument != nullptr);
  if (!instrument) return;

  RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());
  track_state.noteOn(0, *instrument, 220.0f, 0.8f, 60, NoteOrigin::LIVE);
  state.renderBlock(256, song, mixer);
  CHECK(state.getVoiceCount() > 0);

  track_state.noteOff(0);
  for (int block = 0; block < 200; block++) state.renderBlock(256, song, mixer);

  CHECK(state.getVoiceCount() == 0);
}

// How many 256-frame blocks until voiceCount() first reaches 0, capped at
// max_blocks (returns max_blocks if it never does within that budget).
static int
blocksUntilSilent(SongState & state, const Song & song, Mixer & mixer, int max_blocks) {
  for (int block = 0; block < max_blocks; block++) {
    state.renderBlock(256, song, mixer);
    if (state.getVoiceCount() == 0) return block;
  }
  return max_blocks;
}

// render_sf2_multi_region_note_off_eventually_reclaims_every_region's own
// earlier version only checked "does the voice eventually go silent" -
// which passes even if note-off does *nothing* and the sample just plays
// out its own fixed length regardless (a real possibility for a one-shot,
// unlooped region - see stopNote()'s own comment on TSF_LOOPMODE_SUSTAIN
// vs. no loop at all). This compares against a reference where OFF is
// never sent at all - if note-off actually shortens the tail, silence
// should arrive noticeably earlier with it than without.
TEST(render_sf2_multi_region_note_off_actually_shortens_the_tail) {
  auto sf2_path = findSystemSoundFont();
  if (sf2_path.empty()) return;

  ChannelConfiguration config(44100, 1);
  constexpr int kMaxBlocks = 20 * 44100 / 256; // 20s budget

  auto with_off = loadFixtureWithSoundFont("sf2_harp_note_off.xml", sf2_path);
  CHECK(with_off.ok);
  SongState state_with_off(config);
  state_with_off.initialize(with_off.song);
  state_with_off.setIsPlaying(true);
  RecordingMixer mixer_with_off(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());
  int row_samples = config.getSampleInterval(with_off.song.getTempo());
  state_with_off.renderBlock(row_samples, with_off.song, mixer_with_off); // row 0: note on
  CHECK(state_with_off.getVoiceCount() > 1); // genuinely multi-region, not a degenerate single-region preset
  for (int row = 1; row <= 4; row++) state_with_off.renderBlock(row_samples, with_off.song, mixer_with_off); // rows 1-4 (4 = OFF)
  auto blocks_with_off = blocksUntilSilent(state_with_off, with_off.song, mixer_with_off, kMaxBlocks);

  auto without_off = loadFixtureWithSoundFont("sf2_harp_no_note_off.xml", sf2_path);
  CHECK(without_off.ok);
  SongState state_without_off(config);
  state_without_off.initialize(without_off.song);
  state_without_off.setIsPlaying(true);
  RecordingMixer mixer_without_off(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());
  state_without_off.renderBlock(row_samples, without_off.song, mixer_without_off); // row 0: note on, never released
  for (int row = 1; row <= 4; row++) state_without_off.renderBlock(row_samples, without_off.song, mixer_without_off);
  auto blocks_without_off = blocksUntilSilent(state_without_off, without_off.song, mixer_without_off, kMaxBlocks);

  CHECK(blocks_with_off < blocks_without_off);
}

// Same comparison as the pattern-driven version above, but through the
// live-audition path (InstrumentTrackState::noteOn()/noteOff() called
// directly, exactly what Player's PLAY_NOTE/STOP_NOTE do) instead of
// pattern-driven scheduling - the pattern-driven path already proved
// note-off has a real effect on this same multi-region preset, so if this
// one comes out different, the live path itself is where to keep looking.
TEST(render_sf2_multi_region_live_note_off_actually_shortens_the_tail) {
  auto sf2_path = findSystemSoundFont();
  if (sf2_path.empty()) return;

  ChannelConfiguration config(44100, 1);
  constexpr int kMaxBlocks = 20 * 44100 / 256; // 20s budget

  auto with_off = loadFixtureWithSoundFont("sf2_harp_no_note_off.xml", sf2_path); // no pattern content needed - noteOn/noteOff called directly
  CHECK(with_off.ok);
  auto & track_with_off = *with_off.song.getMasterTrack().getChildren()[0];
  SongState state_with_off(config);
  state_with_off.initialize(with_off.song);
  auto & track_state_with_off = dynamic_cast<InstrumentTrackState &>(track_with_off.getState(state_with_off, state_with_off.getSongStructure()));
  auto instrument_with_off = track_state_with_off.getInstrumentSource(with_off.song.getInstrumentPool());
  CHECK(instrument_with_off != nullptr);
  RecordingMixer mixer_with_off(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());
  track_state_with_off.noteOn(0, *instrument_with_off, 220.0f, 0.8f, 60, NoteOrigin::LIVE);
  state_with_off.renderBlock(256, with_off.song, mixer_with_off);
  CHECK(state_with_off.getVoiceCount() > 1); // genuinely multi-region
  track_state_with_off.noteOff(0);
  auto blocks_with_off = blocksUntilSilent(state_with_off, with_off.song, mixer_with_off, kMaxBlocks);

  auto without_off = loadFixtureWithSoundFont("sf2_harp_no_note_off.xml", sf2_path);
  CHECK(without_off.ok);
  auto & track_without_off = *without_off.song.getMasterTrack().getChildren()[0];
  SongState state_without_off(config);
  state_without_off.initialize(without_off.song);
  auto & track_state_without_off = dynamic_cast<InstrumentTrackState &>(track_without_off.getState(state_without_off, state_without_off.getSongStructure()));
  auto instrument_without_off = track_state_without_off.getInstrumentSource(without_off.song.getInstrumentPool());
  CHECK(instrument_without_off != nullptr);
  RecordingMixer mixer_without_off(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());
  track_state_without_off.noteOn(0, *instrument_without_off, 220.0f, 0.8f, 60, NoteOrigin::LIVE);
  state_without_off.renderBlock(256, without_off.song, mixer_without_off);
  // noteOff() deliberately never called here.
  auto blocks_without_off = blocksUntilSilent(state_without_off, without_off.song, mixer_without_off, kMaxBlocks);

  CHECK(blocks_with_off < blocks_without_off);
}

// Reproduction for a reported regression: percussion note-off stopped
// working. Row 4's OFF must actually reach the voice, not just get
// scheduled and dropped.
TEST(render_percussion_note_off_reclaims_the_voice) {
  auto loaded = loadFixture("percussion_note_off.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  SongState state(config);
  state.initialize(loaded.song);
  state.setIsPlaying(true);
  RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

  int row_samples = config.getSampleInterval(loaded.song.getTempo());
  state.renderBlock(row_samples, loaded.song, mixer); // row 0: note on
  CHECK(state.getVoiceCount() > 0);

  for (int row = 1; row <= 4; row++) state.renderBlock(row_samples, loaded.song, mixer); // rows 1-4 (4 = OFF)

  // A little past the note-off row, to allow any release tail to finish.
  for (int row = 0; row < 8; row++) state.renderBlock(row_samples, loaded.song, mixer);

  CHECK(state.getVoiceCount() == 0);
}

TEST(render_tape_degradation_preserves_pan) {
  // The inverse of render_chorus_centers_its_input below: TapeDegradation's
  // own re-encode step uses a real, known position (its own authored
  // azimuth, track-attached here) rather than encodeMonoAsPoint()'s
  // omnidirectional fallback - a hard-right instance should isolate to the
  // right channel exactly the way hard_pan.xml's plain (non-degraded)
  // tracks do, not collapse to center the way chorus_pan.xml's does. At
  // azimuth 90, decodeToStereo()'s cardioid matrix has an exact null on
  // the opposite channel (see AmbisonicEncoding.h), so left should
  // measure as genuine silence, not just "quieter."
  auto loaded = loadFixture("tape_degradation_pan.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  auto left = rms(result, 0), right = rms(result, 1);
  CHECK(right > 1e-4f);
  CHECK(left < 1e-6f);
}

TEST(render_tape_degradation_no_nan_both_sides) {
  // Smoke test: two independently-positioned instances (hard left, hard
  // right) rendering simultaneously stay finite and both sides carry real
  // energy - guards the wow/flutter delay-line integration and the
  // Poisson dropout/click machinery against ever producing NaN/Inf or
  // silently dropping a channel.
  auto loaded = loadFixture("tape_degradation_hard_pan.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  auto left = rms(result, 0), right = rms(result, 1);
  CHECK(left > 1e-4f);
  CHECK(right > 1e-4f);
}

TEST(render_tape_degradation_track_attached_keeps_hissing_after_note_ends) {
  // A track-attached instance is a persistent "machine in the room," not
  // a per-note effect - its hiss/wow/dropout/rumble must keep sounding
  // (and its transport state must keep advancing) straight through a
  // stretch with no notes playing, never freezing/falling silent just
  // because renderChildren() reports zero active children for a block
  // (see effects/TapeDegradation.cpp's own ensureMainChannel() comment).
  // This fixture's one note fully decays to silence by ~0.61s (same
  // envelope shape as center_note.xml/render_envelope_decays_after_hold_and_decay_time
  // above) and the pattern itself ends at 1s - checked well past both.
  auto loaded = loadFixture("tape_degradation_persists_after_note.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  auto tail_rms = windowedRms(result, 0, 3.0f, 4.0f);
  CHECK(tail_rms > 1e-4f);
}

TEST(render_tape_degradation_mellotron_voice_note_off_has_no_click) {
  // The other half of the "voice cuts off abruptly" fix, at the sample
  // level: note-off (row 4, t=0.5s at this fixture's 120bpm/8-row pattern)
  // should never produce a single large sample-to-sample jump - spin-down
  // fades hiss/pitch out over ~120ms (the Mellotron preset's own
  // spinDownMs) and the wow/flutter delay line drains smoothly on top of
  // that, not a hard cut. Checked as the largest per-sample delta in a
  // window spanning well before through well after note-off, against the
  // largest delta seen during the note's own steady sounding portion (a
  // real oscillator waveform already has real, nonzero sample-to-sample
  // deltas - the point isn't "zero delta", it's "nothing bigger at
  // note-off than elsewhere").
  auto loaded = loadFixture("tape_degradation_mellotron_voice.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  auto maxDelta = [&](float start_s, float end_s) {
    auto frames = result.numberOfFrames();
    size_t start = std::min<size_t>(static_cast<size_t>(start_s * result.sampleRate), frames);
    size_t end = std::min<size_t>(static_cast<size_t>(end_s * result.sampleRate), frames);
    float m = 0.0f;
    for (size_t i = start + 1; i < end; i++) {
      auto prev = result.interleaved[(i - 1) * static_cast<size_t>(result.channels)];
      auto cur = result.interleaved[i * static_cast<size_t>(result.channels)];
      m = std::max(m, std::fabs(cur - prev));
    }
    return m;
  };

  float steady_state_delta = maxDelta(0.1f, 0.4f);   // well before note-off, note fully sounding
  float around_note_off_delta = maxDelta(0.45f, 0.7f); // spans note-off (0.5s) through the end of the tail

  CHECK(steady_state_delta > 1e-5f); // sanity: the fixture actually produces real audio
  CHECK(around_note_off_delta < steady_state_delta * 3.0f); // no outlier spike at/after note-off
}

TEST(render_tape_degradation_degrades_send_a_too_not_just_main) {
  // TapeDegradationDsp::applyEffect() runs Main and AuxA/AuxB through the
  // same wow/flutter/hiss/dropout/tone chain (each with its own delay-
  // line/filter state, sharing the same modulation) - same reasoning as
  // Compressor/EnvelopeFilter/Tremolo/BiquadFilter already applying their
  // own shaping to Main and Aux alike, so a send hears the same tape
  // machine the dry signal does. Checked the same way
  // render_track_send_a_reaches_track_state_output does (a RecordingMixer,
  // since the public renderSongOffline() path never exposes pre-bus
  // AuxA/AuxB at all): this fixture's one note fully decays to silence by
  // ~0.61s (same envelope as tape_degradation_persists_after_note.xml
  // above) - AuxA should still carry real (hiss) energy well after that,
  // proving the send isn't just a bypassed clean copy of a now-silent dry
  // signal.
  auto loaded = loadFixture("tape_degradation_send_a.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100);
  SongState state(config);
  state.initialize(loaded.song);
  state.setIsPlaying(true);

  RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

  bool saw_send_a_after_decay = false;
  int block_frames = 256;
  float block_seconds = static_cast<float>(block_frames) / config.getAudioOutSampleRate();
  float elapsed = 0.0f;
  for (int block = 0; block < 200; block++) {
    state.renderBlock(block_frames, loaded.song, mixer);
    elapsed += block_seconds;
    if (elapsed < 0.7f) continue; // skip the window where the note itself is still (or just barely) sounding
    for (auto & data : mixer.accumulated) {
      if (auto * send = data.getChannel(Channel::AuxA)) {
        for (int i = 0; i < data.numberOfFrames(); i++) {
          if (send[i] != 0.0f) { saw_send_a_after_decay = true; break; }
        }
      }
    }
  }

  CHECK(saw_send_a_after_decay);
}

TEST(render_tape_degradation_every_preset_stays_finite_and_audible) {
  // All 8 presets (tape/mellotron/studio/cassette/vinyl/disintegration/
  // dictaphone/opticalFilm) rendered simultaneously, each on its own
  // track - guards every preset's own distinguishing machinery (locked
  // wow, rumble, one-way health decay, the shared breathing/grain
  // envelope follower, the attack swoop/amplitude flutter) against ever
  // producing NaN/Inf or silently going quiet.
  auto loaded = loadFixture("tape_degradation_all_presets.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  auto left = rms(result, 0), right = rms(result, 1);
  CHECK(left > 1e-4f);
  CHECK(right > 1e-4f);
}

TEST(render_chorus_centers_its_input) {
  // Per-track nonlinear effects (Chorus/Distortion) now reduce
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

TEST(render_bitcrush_quantizes_to_expected_levels) {
  // bitcrush_note.xml uses param="1" (1-bit depth -> DistortionDsp's
  // levels = 2^(1-1) = 1, step = 1) with the default drive=1, so every
  // pre-reencode sample is exactly one of {-1, 0, 1} (clamp-then-round).
  // Track-level Distortion reduces its child to MONO and re-encodes with
  // encodeMonoAsPoint (unity gain into W - AmbisonicEncoding.h), and
  // decodeToStereo's boresight normalization for a Y-less (undirected)
  // W-only bus is exactly 0.5 - see decodeToStereo()'s own derivation -
  // so the final stereo samples land on exactly {-0.5, 0, 0.5}. This
  // exercises both halves of the bug docs/known_bugs.md used to track:
  // the "bitchrush" type-string typo (a still-typo'd loader would fall
  // back to HARD_CLIP with param's non-bitcrush default of 0, clipping
  // everything to silence - caught by the rms check below) and
  // DistortionType::BITCRUSH's DSP case actually doing something (an
  // empty case would leave the signal unquantized, failing the discrete-
  // level check).
  auto loaded = loadFixture("bitcrush_note.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 0);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  CHECK(rms(result, 0) > 0.05f);

  auto frames = result.numberOfFrames();
  size_t start = std::min<size_t>(static_cast<size_t>(0.05f * result.sampleRate), frames);
  bool all_quantized = true;
  for (size_t i = start; i < frames; i++) {
    for (int ch = 0; ch < result.channels; ch++) {
      auto v = result.interleaved[i * static_cast<size_t>(result.channels) + static_cast<size_t>(ch)];
      auto nearest = std::round(v / 0.5f) * 0.5f;
      if (std::fabs(v - nearest) > 1e-4f) all_quantized = false;
    }
  }
  CHECK(all_quantized);
}

TEST(render_envelope_decays_after_hold_and_decay_time) {
  // center_note.xml wraps its oscillator in <envelope attack=.01 hold=.3
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
    for (size_t i = start; i < end; i++) sum += std::pow(result.interleaved[i * static_cast<size_t>(result.channels)], 2);
    return static_cast<float>(std::sqrt(sum / (end - start)));
  };

  auto early = windowRms(0.05f, 0.15f); // after attack, during hold at full level
  auto late = windowRms(1.0f, 1.1f);    // long after decay reached sustain=0

  CHECK(early > 1e-3f);
  CHECK(late < early * 0.01f);
}

// Regression test for two compounding click bugs found together: (1)
// EnvelopeFilterState had no fastRelease() override, so
// InstrumentTrackState::retriggerVoices()'s identity-based fast-release
// path fell through to TrackState's generic default - recursing straight
// into the wrapped OscillatorVoice and killing it instantly (freq_ = 0, no
// ramp) instead of "let children play, fade the wrapping envelope" like
// stopNote() already does. (2) Even with that fixed, EnvelopeFilterState::
// applyEffect() (and SoundFontVoice::render()'s identical pattern) applied
// the envelope's gain as one flat value per RENDER_EFFECTSAMPLEBLOCK (64
// samples), which is coarse enough relative to the ~10ms/441-sample fast
// release's exponential decay (~26% level drop per block) to produce an
// audible staircase - confirmed by tracing a discontinuity to an exact
// block boundary. envelope_oscillator_rapid_retrigger.xml retriggers the
// same identity every pattern row at tempo=900 (~16.7ms apart, well inside
// the fast-release window), exercising both fixes together; without them
// this produces sample-to-sample jumps around 0.4-0.6 (out of a [-1,1]
// range) at retrigger/fast-release boundaries.
TEST(render_envelope_oscillator_rapid_retrigger_has_no_click) {
  auto loaded = loadFixture("envelope_oscillator_rapid_retrigger.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));
  CHECK(result.numberOfFrames() > 1);

  auto frames = result.numberOfFrames();
  float max_delta = 0.0f;
  for (size_t i = 1; i < frames; i++) {
    float delta = std::fabs(result.interleaved[i * static_cast<size_t>(result.channels)] - result.interleaved[(i - 1) * static_cast<size_t>(result.channels)]);
    max_delta = std::max(max_delta, delta);
  }

  // A smooth sine at C-4 (~262Hz) through this envelope never needs a
  // sample-to-sample jump anywhere near this large - genuine audio content
  // (including two overlapping voices during the fast-release/attack
  // crossfade) stays under ~0.05 in practice; the pre-fix bugs produced
  // jumps of 0.4-0.6.
  CHECK(max_delta < 0.1f);
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
  // Tolerance 0.25, not 0.15: this compares two independent windows of
  // pink noise (Noise's own per-note seed - dsp/NoiseGenerator.h - is
  // deterministic per note but still just one specific realization, not
  // identical energy every time), so some run-to-run RMS spread between
  // two otherwise-equivalent positions is expected, not a spatial bug.
  CHECK(az135_r > az135_l);
  CHECK_NEAR(diffFor(36), diffFor(12), std::fabs(diffFor(12)) * 0.25f);

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
  // channel-count bump (AmbisonicEncoding.h, AudioBuffer.h) and the new
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
  // instrument with sendA=-6.0206 dB (~0.5 linear) configured on its <track> - confirms the
  // whole propagation path (InstrumentVoice -> InstrumentTrackState ->
  // SongState) actually carries real SendA energy, using a RecordingMixer
  // to inspect the per-track AudioBuffer the real Mixer would otherwise
  // silently drop (see Mixer.h).
  auto loaded = loadFixture("send_a_oscillator.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100);
  SongState state(config);
  state.initialize(loaded.song);
  state.setIsPlaying(true);

  RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

  bool saw_send_a = false;
  for (int block = 0; block < 20 && !saw_send_a; block++) {
    state.renderBlock(256, loaded.song, mixer);
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
  // the final, already-decoded 2-channel device output. SongState::renderBlock()
  // accumulates the send bus last (after every per-track accumulate()), so
  // accumulated.back() after each render() call is exactly that entry.
  auto loaded = loadFixture("send_a_oscillator.xml");
  CHECK(loaded.ok);

  for (int order : { 1, 2 }) {
    ChannelConfiguration config(44100, order);
    SongState state(config);
    state.initialize(loaded.song);
    state.setIsPlaying(true);

    RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

    double energy_beyond_wy = 0.0;
    for (int block = 0; block < 60; block++) {
      state.renderBlock(256, loaded.song, mixer);
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

TEST(render_dirac_heatmap_peak_matches_encoded_azimuth_sweep) {
  // Reuses ambisonic_directions.xml (see
  // render_ambisonic_directions_produce_distinguishable_output above for
  // the same fixture, row-to-seconds convention, and firing schedule) -
  // broadband pink noise, ideal for DiracAnalyzer since it excites every
  // band rather than just one. Feeds the real pre-decode ambisonic bus
  // into DiracAnalyzer directly at the DSP level - this test doesn't need
  // VisualizationThread/events at all, same as every other RenderTests.cpp
  // test that doesn't need the real audio-device/UI plumbing.
  //
  // RecordingMixer::accumulated.back() alone is NOT that bus - it's only
  // SongState::renderBlock()'s *last* accumulate() call, the shared send bus's
  // own AuxA/AuxB-derived contribution (SongState.h), which is silent
  // here since this fixture's tracks configure no sends at all. A real
  // Mixer sums every accumulate() call (one per top-level track, plus the
  // send bus) into one buffer internally; mixNamed() (AudioBuffer.h) is
  // the same aux-tolerant summing operation, reused below to replicate
  // that summing by hand over every entry in `accumulated`.
  auto loaded = loadFixture("ambisonic_directions.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1); // order 1 = exactly 4 channels (W/Y/Z/X), matching DiracAnalyzer's input shape with no truncation needed
  SongState state(config);
  state.initialize(loaded.song);
  state.setIsPlaying(true);

  RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());
  DiracAnalyzer analyzer(config.getAudioOutSampleRate());

  // Track 10 (distance=2) is a distance-invariance check, not part of the
  // azimuth sweep (same azimuth/elevation as track 0) - excluded here.
  // Each check point is 0.6s after that track's note-on: well into its
  // envelope's hold phase (attack 0.02s, hold to 0.92s - see the
  // fixture's own comment on why pink noise needs real duration to
  // develop spectral cues) and comfortably past DiracAnalyzer's own
  // ~100ms-time-constant smoothing convergence window
  // (dsp/DiracAnalyzerTests.cpp's own tests use the same ~465ms/40-hop
  // convergence estimate), while staying well before the next track's
  // onset 1.5s later (avoiding contamination).
  struct Expected { float time_s, azimuth, elevation; };
  std::vector<Expected> expected = {
    { 0 * 0.125f + 0.6f,     0.0f,   0.0f },
    { 12 * 0.125f + 0.6f,   45.0f,   0.0f },
    { 24 * 0.125f + 0.6f,   90.0f,   0.0f },
    { 36 * 0.125f + 0.6f,  135.0f,   0.0f },
    { 48 * 0.125f + 0.6f,  180.0f,   0.0f },
    { 60 * 0.125f + 0.6f, -135.0f,   0.0f },
    { 72 * 0.125f + 0.6f,  -90.0f,   0.0f },
    { 84 * 0.125f + 0.6f,  -45.0f,   0.0f },
    { 96 * 0.125f + 0.6f,    0.0f,  90.0f },
    { 108 * 0.125f + 0.6f,   0.0f, -90.0f },
  };

  size_t next_check = 0;
  int frames_rendered = 0;
  int sample_rate = config.getAudioOutSampleRate();

  while (next_check < expected.size()) {
    state.renderBlock(256, loaded.song, mixer);
    CHECK(!mixer.accumulated.empty());

    AudioBuffer bus(static_cast<short>(config.numberOfChannels()), 256);
    bus.zero(); // the raw-count constructor leaves the buffer uninitialized (aligned_alloc, not calloc) - mixNamed() below accumulates with +=
    for (auto & data : mixer.accumulated) bus.mixNamed(data);

    analyzer.process(bus);
    frames_rendered += 256;

    while (next_check < expected.size() && frames_rendered >= static_cast<int>(expected[next_check].time_s * static_cast<float>(sample_rate))) {
      auto & exp = expected[next_check];

      // Peak grid cell -> its bin-center azimuth/elevation, the inverse of
      // DiracAnalyzer.cpp's own az_pos/el_pos splat-target formulas.
      auto & grid = analyzer.getGrid();
      int peak_cell = 0;
      float peak_value = grid[0];
      for (int c = 1; c < DiracAnalyzer::kGridSize; c++) {
        if (grid[static_cast<size_t>(c)] > peak_value) { peak_value = grid[static_cast<size_t>(c)]; peak_cell = c; }
      }
      int az_bin = peak_cell % DiracAnalyzer::kAzimuthBins;
      int el_bin = peak_cell / DiracAnalyzer::kAzimuthBins;
      float peak_azimuth = (static_cast<float>(az_bin) + 0.5f) * 360.0f / static_cast<float>(DiracAnalyzer::kAzimuthBins) - 180.0f;
      float peak_elevation = (static_cast<float>(el_bin) + 0.5f) * 10.0f - 90.0f;

      CHECK(peak_value > 0.0f);

      // Azimuth is physically undefined exactly at the poles (every
      // azimuth value describes the same point once elevation is +-90) -
      // the straight-up/straight-down tracks only assert elevation, not
      // the fixture's otherwise-arbitrary azimuth="0" on those two.
      if (std::fabs(exp.elevation) < 89.0f) {
        // Azimuth wraps - compare via the shortest angular distance, not
        // raw subtraction (e.g. 179 vs -179 are 2 degrees apart, not 358).
        float az_diff = fmodf(peak_azimuth - exp.azimuth + 540.0f, 360.0f) - 180.0f;
        CHECK(std::fabs(az_diff) < 20.0f);
      }
      CHECK_NEAR(peak_elevation, exp.elevation, 20.0f);

      next_check++;
    }
  }
}

TEST(render_track_send_b_reaches_track_state_output) {
  // SendB's sibling of render_track_send_a_reaches_track_state_output -
  // confirms the multi-tap delay's mono input actually gets fed real
  // energy through the same InstrumentVoice -> InstrumentTrackState ->
  // SongState propagation path, using send_b_oscillator.xml (identical to
  // send_a_oscillator.xml except sendB="-6.0206" (dB, ~0.5 linear) instead of sendA).
  auto loaded = loadFixture("send_b_oscillator.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100);
  SongState state(config);
  state.initialize(loaded.song);
  state.setIsPlaying(true);

  RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

  bool saw_send_b = false;
  for (int block = 0; block < 20 && !saw_send_b; block++) {
    state.renderBlock(256, loaded.song, mixer);
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
  auto loaded = loadFixture("send_b_oscillator.xml");
  CHECK(loaded.ok);

  for (int order : { 1, 2 }) {
    ChannelConfiguration config(44100, order);
    SongState state(config);
    state.initialize(loaded.song);
    state.setIsPlaying(true);

    RecordingMixer mixer(static_cast<short>(config.numberOfChannels()), config.getAudioOutSampleRate());

    double energy_beyond_wy = 0.0;
    for (int block = 0; block < 60; block++) {
      state.renderBlock(256, loaded.song, mixer);
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
  // send_a_oscillator.xml is identical to center_note.xml except for
  // sendA="-6.0206" (dB, ~0.5 linear) on its one track - any output difference between them is
  // attributable entirely to SendBusProcessor's shared reverb, which now
  // actually reaches the final mix (Phase 1 only proved the plumbing
  // reached the mixer's accumulator - see
  // render_track_send_a_reaches_track_state_output above - without ever
  // becoming audible, since Mixer::encode() deliberately drops it).
  auto with_send = loadFixture("send_a_oscillator.xml");
  auto without_send = loadFixture("center_note.xml");
  CHECK(with_send.ok);
  CHECK(without_send.ok);

  ChannelConfiguration config(44100, 1);
  auto result_with = renderSongOffline(with_send.song, config);
  auto result_without = renderSongOffline(without_send.song, config);
  CHECK(!hasNonFiniteSample(result_with));
  CHECK(!hasNonFiniteSample(result_without));

  // Both fixtures wrap their oscillator in the same <envelope attack=.01
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
  // send_b_oscillator.xml is identical to center_note.xml except for
  // sendB="-6.0206" (dB, ~0.5 linear) on its one track - any output difference between them is
  // attributable entirely to SendBusProcessor's shared multi-tap delay.
  // Same methodology as render_send_a_produces_audible_reverb_tail.
  auto with_send = loadFixture("send_b_oscillator.xml");
  auto without_send = loadFixture("center_note.xml");
  CHECK(with_send.ok);
  CHECK(without_send.ok);

  ChannelConfiguration config(44100, 1);
  auto result_with = renderSongOffline(with_send.song, config);
  auto result_without = renderSongOffline(without_send.song, config);
  CHECK(!hasNonFiniteSample(result_with));
  CHECK(!hasNonFiniteSample(result_without));

  // Both fixtures wrap their oscillator in the same <envelope attack=.01
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

TEST(render_haze_produces_audible_diffuse_bed) {
  // haze_oscillator.xml is identical to center_note.xml except for
  // sendB="-6.0206" (dB, ~0.5 linear) on its one track and a <haze preset="crunch"/> occupying
  // slot B instead of the default delay - end-to-end coverage for
  // plans/drum-bus-saturator.md's whole feature (drive/shape/bias/
  // bandpass/oversample/tilt/auto-gain/pre-delay/diffuse-encode), not a
  // re-check of any one stage's own already-covered math (see
  // HazeTests.cpp/AmbisonicDiffuseEncoderTests.cpp/HalfbandFilterTests.cpp
  // for that). Same methodology as render_send_b_produces_audible_delay_echo.
  auto with_send = loadFixture("haze_oscillator.xml");
  auto without_send = loadFixture("center_note.xml");
  CHECK(with_send.ok);
  CHECK(without_send.ok);

  ChannelConfiguration config(44100, 3); // 3rd order - exercises all 16 channels
  auto result_with = renderSongOffline(with_send.song, config);
  auto result_without = renderSongOffline(without_send.song, config);
  CHECK(!hasNonFiniteSample(result_with));
  CHECK(!hasNonFiniteSample(result_without));

  // Same envelope as every other fixture here - fully silent by ~0.66s -
  // a window well past that isolates Haze's diffuse return (delayed by
  // `crunch`'s 1/128 predelay, ~16ms at this fixture's 120bpm tempo) from
  // the dry note.
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
  // Two otherwise-identical tracks (oscillator + envelope, sendA=-6.0206 dB (~0.5 linear))
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
      state.renderBlock(256, song, mixer);
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
  // sendMain="-100" (hard off) alongside sendA="-6.0206" (dB, ~0.5 linear) on the same track (SendLevels.h,
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
  // and the shared send bus's own separate contribution (SongState::renderBlock()
  // accumulates both) - the bus's own accumulator always structurally has
  // Main (it's a plain always-Main-present accumulator, not derived from
  // children - see bus/SendBusProcessor.cpp), so hasChannel(Channel::Main)
  // alone can't distinguish "this specific track's Main" from "the bus's
  // own Main" across all entries; main_peak still correctly stays near
  // zero either way, since sendA-driven bus content this early has ~no W
  // energy yet in this fixture's short window.
  float main_peak = 0.0f, send_a_peak = 0.0f;
  for (int block = 0; block < 20; block++) {
    state.renderBlock(256, loaded.song, mixer);
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

TEST(render_arpeggiator_steps_through_a_pattern_authored_chord) {
  // Phase 2 (plans/arpeggiator.md): InstrumentTrackState's own
  // pending-events note-on (pattern/song-driven playback, not live
  // audition) now calls the same virtual noteOn() Player.cpp does, so an
  // ArpeggiatorState's override picks it up here too - the concrete
  // end-to-end regression test that a chord authored directly into an
  // <arpeggiatorTrack>'s pattern row actually arpeggiates during real
  // (offline-rendered) playback, not just via direct
  // ArpeggiatorState::noteOn() calls (tests/ArpeggiatorStateTests.cpp).
  auto loaded = loadFixture("arpeggiator_pattern_chord.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  // tempo 240, noteDuration=gate=2 rows -> a 0.125s step, legato (no gap):
  // step 0 (C-4, ~261.6Hz) in [0, 0.125), step 1 (E-4, ~329.6Hz) in
  // [0.125, 0.25), step 2 (G-4, ~392.0Hz) in [0.25, 0.375) - windows below
  // sit well inside each interval, clear of both the exact boundary (a
  // step/gate transition is only resolved lazily, at the start of the next
  // render(int) call - see ArpeggiatorState.h) and renderSongOffline()'s
  // own 1024-frame (~23ms) block granularity.
  CHECK(windowedRms(result, 0, 0.04f, 0.09f) > 1e-4f);
  CHECK(windowedRms(result, 0, 0.165f, 0.215f) > 1e-4f);
  CHECK(windowedRms(result, 0, 0.29f, 0.34f) > 1e-4f);

  auto rate0 = windowedZeroCrossingRate(result, 0, 0.04f, 0.09f);
  auto rate1 = windowedZeroCrossingRate(result, 0, 0.165f, 0.215f);
  auto rate2 = windowedZeroCrossingRate(result, 0, 0.29f, 0.34f);

  // Ascending steps, not one static tone - the concrete "does the
  // stepping logic actually step" check for the pattern-driven path.
  // Loose bounds (this is a zero-crossing estimate, not a real pitch
  // detector) around the expected ratios (E-4/C-4 ~= 1.26, G-4/C-4 ~= 1.5).
  CHECK(rate1 > rate0 * 1.1f);
  CHECK(rate1 < rate0 * 1.45f);
  CHECK(rate2 > rate0 * 1.3f);
  CHECK(rate2 < rate0 * 1.7f);
}

TEST(render_pattern_shorter_than_song_repeats) {
  // A track's own <pattern length="4"> in an 8-row song plays its 4 real
  // rows (note on at row 0, off at row 2), then repeats them verbatim at
  // rows 4-7 -
  // rows this fixture never authors at all (Pattern::getEffectiveRow()'s
  // own modulo, not literal duplicated content). tempo 240 -> a 0.0625s
  // row, so each quarter-pattern interval is 0.125s wide.
  auto loaded = loadFixture("pattern_shorter_than_song_repeats.xml");
  CHECK(loaded.ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(loaded.song, config);
  CHECK(!hasNonFiniteSample(result));

  CHECK(windowedRms(result, 0, 0.03f, 0.08f) > 1e-4f);   // rows 0-1: on (real content)
  CHECK(windowedRms(result, 0, 0.16f, 0.21f) < 1e-4f);   // rows 2-3: off (real content)
  CHECK(windowedRms(result, 0, 0.28f, 0.33f) > 1e-4f);   // rows 4-5: on again - the repeat itself
  CHECK(windowedRms(result, 0, 0.41f, 0.46f) < 1e-4f);   // rows 6-7: off again
}

// Golden-render regression test - hashes raw output samples for fixtures
// that exercise every HashField-derived randomization site (NoteMultiplier's
// unison/detune jitter and pattern note start-phase -
// ambisonic_envelopefilter_notemultiplier.xml; TapeDegradation's
// per-instance seed plus the per-sample hiss/dropout/click stream that
// seed drives - tape_degradation_all_presets.xml; ArpeggiatorState's
// per-step start-phase - arpeggiator_pattern_chord.xml). Not a claim that
// these specific hash values are "correct" in any musical sense, just
// that they're what this exact build deterministically produces - this
// test's job is to catch an *accidental* change: a stray call reordered,
// a miscomputed coordinate, or a reintroduced rand()/non-portable
// <random> distribution landing back in the render path.
//
// Bit-exact output isn't claimed across every possible build - -ffast-math
// permits reassociation that can vary by compiler/optimization level, and
// even a fixed, portable -march target (CMakeLists.txt's SYNTH_MARCH) only
// pins *which instructions get used*, not identical results across
// genuinely different CPU architectures (x86-64 vs ARM's FPUs don't
// promise bit-identical rounding for the same portable C++ source, no
// matter how "portable" that source is). So the constants below are only
// meaningful, and only checked, against one specific canonical
// configuration - "SYNTH_MARCH=x86-64-v2" (what CI pins) - matched via the
// SYNTH_MARCH compile definition (tests/CMakeLists.txt). Any other build
// (a local dev build's default -march=native, a future ARM CI job, ...)
// skips the exact-hash assertions below - still renders and checks
// finiteness, just doesn't expect to match a hash computed on different
// hardware. Update the constants (rebuild with -DSYNTH_MARCH=x86-64-v2 to
// reproduce them) in the same commit as any change that legitimately
// alters one of these fixtures' rendered output.
//
TEST(render_golden_hash_catches_randomization_regressions) {
  bool canonical_arch = std::string(SYNTH_MARCH) == "x86-64-v2";

  ChannelConfiguration config(44100, 1);

  auto notemultiplier = loadFixture("ambisonic_envelopefilter_notemultiplier.xml");
  CHECK(notemultiplier.ok);
  auto notemultiplier_result = renderSongOffline(notemultiplier.song, config, MixerType::AMBISONIC_STEREO);
  CHECK(!hasNonFiniteSample(notemultiplier_result));
  if (canonical_arch) CHECK(hashSamples(notemultiplier_result) == 0xcf48af3c3844c85eull);

  auto tape = loadFixture("tape_degradation_all_presets.xml");
  CHECK(tape.ok);
  auto tape_result = renderSongOffline(tape.song, config);
  CHECK(!hasNonFiniteSample(tape_result));
  if (canonical_arch) CHECK(hashSamples(tape_result) == 0x125cc894073b2955ull);

  auto arp = loadFixture("arpeggiator_pattern_chord.xml");
  CHECK(arp.ok);
  auto arp_result = renderSongOffline(arp.song, config);
  CHECK(!hasNonFiniteSample(arp_result));
  if (canonical_arch) CHECK(hashSamples(arp_result) == 0xdcd3a248cb0ce779ull);
}
