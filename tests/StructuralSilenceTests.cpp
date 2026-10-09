#include "TestFramework.h"

#include "../src/ambisonic/AmbisonicDecoders.h"
#include "../src/ambisonic/AmbisonicBinauralMixer.h"
#include "../src/ambisonic/AmbisonicMagLSDecoder.h"
#include "../src/bus/BusEffect.h"
#include "../src/bus/SendBusProcessor.h"
#include "../src/Controller.h"
#include "../src/playback/AudioBlockEvent.h"
#include "../src/playback/VisualizationResultEvent.h"
#include "../src/playback/VisualizationThread.h"

#include <cmath>
#include <memory>

namespace {

AudioBuffer ambisonicImpulse(int channels, int frames) {
  AudioBuffer b(static_cast<short>(channels), frames);
  b.zero();
  b.getChannelData(0)[0] = 1.0f;
  if (channels > 1) b.getChannelData(1)[0] = 0.5f;
  return b;
}

bool allZero(const AudioBuffer & b) {
  for (int c = 0; c < b.numberOfChannels(); c++) {
    for (int i = 0; i < b.numberOfFrames(); i++)
      if (b.getChannelData(c)[i] != 0.0f) return false;
  }
  return true;
}

// Adds a settable constant to every channel, standing in for an effect.
class LevelEffect : public BusEffect {
 public:
  explicit LevelEffect(int sampleRate) : BusEffect(sampleRate) {}
  void process(const float *, int) override {}
  int getNumTaps() const override { return 0; }
  const float * getTap(int) const override { return nullptr; }
  SphericalPosition getTapDirection(int) const override { return SphericalPosition{}; }
  void encodeDirect(AudioBuffer & bus, int frames) override {
    for (int c = 0; c < bus.regularChannelCount(); c++) {
      for (int i = 0; i < frames; i++) bus.getChannelData(c)[i] += level;
    }
  }
  float level = 0.0f;
};

} // namespace

TEST(send_bus_is_inaudible_when_nothing_flows_through_it) {
  ChannelConfiguration config(44100, 1);
  SendBusProcessor bus(config);
  AudioBuffer a(1, 256), b(1, 256);
  a.zero();
  b.zero();
  bus.process(a, b, 256);
  CHECK(!bus.isAudible());
}

TEST(send_bus_denormal_guard_stays_far_below_the_audible_floor) {
  CHECK(kDenormalGuard * 1e6f < kBusAudibleFloor);
}

TEST(send_bus_holds_a_decayed_tail_before_dropping_it) {
  ChannelConfiguration config(44100, 1);
  SendBusProcessor bus(config);
  auto effect = std::make_unique<LevelEffect>(config.getAudioOutSampleRate());
  auto * level = effect.get();
  bus.setSlotEffect(SendBusProcessor::kSlotA, std::move(effect));

  const int frames = 441;
  AudioBuffer a(1, frames), b(1, frames);
  a.zero();
  b.zero();

  level->level = 0.5f;
  bus.process(a, b, frames);
  CHECK(bus.isAudible());

  // The level falls to the denormal guard's: the tail is not cut at once...
  level->level = kDenormalGuard;
  bus.process(a, b, frames);
  CHECK(bus.isAudible());

  // ...but is dropped once the hold has run out.
  int blocks = static_cast<int>(kBusAudibleHoldSeconds * 44100.0f) / frames + 2;
  for (int i = 0; i < blocks; i++) bus.process(a, b, frames);
  CHECK(!bus.isAudible());

  // A returning signal is audible again at once.
  level->level = 0.5f;
  bus.process(a, b, frames);
  CHECK(bus.isAudible());
}

TEST(stereo_mixer_with_nothing_accumulated_has_an_empty_raw_bus_and_encodes_zeros) {
  AmbisonicStereoMixer mixer(4, 44100);
  mixer.reset();
  mixer.accumulate(AudioBuffer(0, false, false, 64)); // establishes the block size, adds nothing
  auto & raw = mixer.getRawBus();
  CHECK(!raw.hasChannel(Channel::Main));
  CHECK(raw.numberOfChannels() == 0);
  CHECK(raw.numberOfFrames() == 64);

  auto out = mixer.encode();
  CHECK(out.numberOfChannels() == 2);
  CHECK(out.numberOfFrames() == 64);
  CHECK(allZero(out));
}

TEST(stereo_mixer_raw_bus_follows_what_was_accumulated) {
  AmbisonicStereoMixer mixer(4, 44100);
  mixer.reset();
  mixer.accumulate(ambisonicImpulse(4, 64));
  CHECK(mixer.getRawBus().hasChannel(Channel::Main));
  CHECK(mixer.getRawBus().regularChannelCount() == 4);
  CHECK(mixer.getRawBus().getChannelData(0)[0] == 1.0f);
  CHECK(!allZero(mixer.encode()));

  // The next block starts empty again, and the old content is gone.
  mixer.reset();
  mixer.accumulate(AudioBuffer(0, false, false, 64));
  CHECK(!mixer.getRawBus().hasChannel(Channel::Main));
  CHECK(allZero(mixer.encode()));
  mixer.reset();
  mixer.accumulate(ambisonicImpulse(4, 64));
  CHECK(mixer.getRawBus().getChannelData(1)[0] == 0.5f);
  CHECK(mixer.getRawBus().getChannelData(0)[1] == 0.0f);
}

TEST(mixer_aux_only_input_does_not_count_as_accumulated) {
  AmbisonicStereoMixer mixer(4, 44100);
  mixer.reset();
  mixer.accumulate(AudioBuffer(0, true, true, 64));
  CHECK(!mixer.getRawBus().hasChannel(Channel::Main));
  CHECK(!mixer.getRawBus().hasChannel(Channel::AuxA));
  CHECK(!mixer.getRawBus().hasChannel(Channel::AuxB));
}

#ifdef SYNTH_HAVE_LIBMYSOFA

namespace {

// Feeds `decoder` an impulse, then blocks that add nothing: either
// structurally (`empty`) or as full-shape zeros. Returns every block out.
std::vector<AudioBuffer> impulseThenSilence(Mixer & decoder, int channels, int frames, int blocks, bool empty) {
  std::vector<AudioBuffer> out;
  for (int n = 0; n < blocks; n++) {
    decoder.reset();
    if (n == 0) {
      decoder.accumulate(ambisonicImpulse(channels, frames));
    } else if (empty) {
      decoder.accumulate(AudioBuffer(0, false, false, frames));
    } else {
      AudioBuffer zeros(static_cast<short>(channels), frames);
      zeros.zero();
      decoder.accumulate(zeros);
    }
    out.push_back(decoder.encode());
  }
  return out;
}

void checkTailMatchesExplicitZeros(Mixer & structural, Mixer & explicit_zeros, int channels) {
  const int frames = 64, blocks = 40;
  auto a = impulseThenSilence(structural, channels, frames, blocks, true);
  auto b = impulseThenSilence(explicit_zeros, channels, frames, blocks, false);

  bool tail_heard = false;
  for (int n = 1; n < blocks; n++) {
    for (int c = 0; c < 2; c++) {
      for (int i = 0; i < frames; i++) {
        CHECK(a[static_cast<size_t>(n)].getChannelData(c)[i] == b[static_cast<size_t>(n)].getChannelData(c)[i]);
        if (a[static_cast<size_t>(n)].getChannelData(c)[i] != 0.0f) tail_heard = true;
      }
    }
  }
  CHECK(tail_heard);        // the tail was drained into the empty blocks, not cut
  CHECK(allZero(a.back())); // and runs out, leaving a full block of zeros
  CHECK(a.back().numberOfFrames() == frames);
  CHECK(a.back().numberOfChannels() == 2);
}

} // namespace

TEST(magls_decoder_drains_its_tail_through_empty_blocks) {
  AmbisonicMagLSDecoder a(4, 44100), b(4, 44100);
  if (!a.isReady()) return;
  checkTailMatchesExplicitZeros(a, b, 4);
  CHECK(!a.getRawBus().hasChannel(Channel::Main));
}

TEST(binaural_mixer_drains_its_tail_through_empty_blocks) {
  AmbisonicBinauralMixer a(4, 44100), b(4, 44100);
  if (!a.isReady()) return;
  checkTailMatchesExplicitZeros(a, b, 4);
}

#endif // SYNTH_HAVE_LIBMYSOFA

TEST(visualization_thread_empty_bus_gives_zero_result_then_stops_analyzing) {
  Controller controller{ChannelConfiguration(44100, 1)};
  VisualizationThread thread(&controller);
  const int frames = 256;
  thread.configure(44100, frames);

  auto push = [&](AudioBuffer raw) {
    AudioBuffer master(2, frames);
    master.zero();
    AudioBlockEvent ev(std::move(master), std::move(raw), AudioBuffer(0, false, false, frames), AudioBuffer(0, false, false, frames));
    thread.handleAudioBlockEvent(ev);
    auto popped = controller.getUIEventQueue().pop();
    return std::unique_ptr<VisualizationResultEvent>(dynamic_cast<VisualizationResultEvent *>(popped.release()));
  };

  // Some sound first, so the analyzers hold something to clear.
  AudioBuffer loud(4, frames);
  for (int c = 0; c < 4; c++) {
    for (int i = 0; i < frames; i++) loud.getChannelData(c)[i] = sinf(static_cast<float>(i) * 0.3f);
  }
  for (int n = 0; n < 40; n++) {
    auto r = push(loud);
    CHECK(r != nullptr);
    if (r) CHECK(r->getChannelLoudness()[0] > 0.1f);
  }

  // Then silence by structure: every block gets a result, loudness zero,
  // and the analyzers end up showing zeros.
  std::vector<float> last_fft;
  std::vector<float> last_grid;
  int blocks_with_analysis_after_settling = 0;
  const int silent_blocks = 1500; // ~9 s: the 100 ms window and DirAC's smoothing have run out
  for (int n = 0; n < silent_blocks; n++) {
    auto r = push(AudioBuffer(0, false, false, frames));
    CHECK(r != nullptr);
    if (!r) return;
    CHECK(r->getChannelLoudness().size() == 6); // 4 regular + AuxA + AuxB
    for (float v : r->getChannelLoudness()) CHECK(v == 0.0f);
    if (!r->getFFT().empty()) last_fft = r->getFFT();
    if (r->hasDiracGrid()) last_grid.assign(r->getDiracGrid().begin(), r->getDiracGrid().end());
    if (n >= silent_blocks - 20 && (!r->getFFT().empty() || r->hasDiracGrid())) blocks_with_analysis_after_settling++;
  }
  CHECK(!last_fft.empty());
  for (float v : last_fft) CHECK_NEAR(v, -100.0f, 1e-3f);
  CHECK(!last_grid.empty());
  for (float v : last_grid) CHECK(v == 0.0f);
  // Settled: nothing is decoded or analyzed any more, so no payload at all.
  CHECK(blocks_with_analysis_after_settling == 0);

  // Sound coming back is analyzed again.
  bool fft_again = false;
  for (int n = 0; n < 40; n++) {
    auto r = push(loud);
    if (r && !r->getFFT().empty()) fft_again = true;
  }
  CHECK(fft_again);
}
