#include "TestFramework.h"

#include "../src/bus/SendBusProcessor.h"
#include "../src/bus/BusEffectRegistry.h"
#include "../src/bus/Haze.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/audio/AudioBuffer.h"
#include "../src/dsp/NoiseGenerator.h"
#include "../src/dsp/PinkNoiseFilter.h"

using namespace std;

namespace {

// Minimal test-only BusEffect exercising only the direct-channel path
// (BusEffect::encodeDirect(), see plans/drum-bus-saturator.md) - zero
// taps, so it would contribute nothing at all via the ordinary
// point-source tap loop, the thing this test needs to distinguish from.
class FakeDirectEffect : public BusEffect {
 public:
  explicit FakeDirectEffect(int sampleRate) : BusEffect(sampleRate) { }
  void process(const float *, int) override { }
  int getNumTaps() const override { return 0; }
  const float * getTap(int) const override { return nullptr; }
  SphericalPosition getTapDirection(int) const override { return SphericalPosition{}; }

  void encodeDirect(AudioBuffer & busAmbisonic, int frames) override {
    int n = busAmbisonic.regularChannelCount();
    for (int c = 0; c < n; c++) {
      auto dst = busAmbisonic.getChannelData(c);
      for (int i = 0; i < frames; i++) dst[i] += kMarker;
    }
  }

  static constexpr float kMarker = 0.25f;
};

AudioBuffer silentMono(int frames) {
  AudioBuffer d(1, frames);
  d.zero();
  return d;
}

}

TEST(send_bus_processor_calls_encode_direct_for_slot_a) {
  ChannelConfiguration config(44100, 1); // 4 channels
  SendBusProcessor bus(config);
  bus.setSlotEffect(SendBusProcessor::kSlotA, make_unique<FakeDirectEffect>(config.getAudioOutSampleRate()));

  int frames = 64;
  auto auxA = silentMono(frames);
  auto auxB = silentMono(frames);
  bus.process(auxA, auxB, frames);

  auto & out = bus.getBusAmbisonic();
  CHECK(out.numberOfChannels() == 4);
  for (int c = 0; c < 4; c++) {
    for (int i = 0; i < frames; i++) {
      CHECK_NEAR(out.getChannelData(c)[i], FakeDirectEffect::kMarker, 1e-6f);
    }
  }
}

TEST(send_bus_processor_calls_encode_direct_for_slot_b_too) {
  ChannelConfiguration config(44100, 1);
  SendBusProcessor bus(config);
  bus.setSlotEffect(SendBusProcessor::kSlotB, make_unique<FakeDirectEffect>(config.getAudioOutSampleRate()));

  int frames = 32;
  auto auxA = silentMono(frames);
  auto auxB = silentMono(frames);
  bus.process(auxA, auxB, frames);

  auto & out = bus.getBusAmbisonic();
  for (int i = 0; i < frames; i++) {
    CHECK_NEAR(out.getChannelData(0)[i], FakeDirectEffect::kMarker, 1e-6f);
  }
}

TEST(send_bus_processor_default_effects_have_no_direct_contribution) {
  // NullBusEffect/FDNReverb/MultiTapDelay never override encodeDirect() -
  // the default no-op must leave the accumulator exactly as the ordinary
  // tap loop left it (this is really a regression guard for the new hook
  // itself: adding it must not perturb every existing bus effect).
  ChannelConfiguration config(44100, 1);
  SendBusProcessor bus(config);
  // Compiled default: NullBusEffect in both slots via this class's own
  // constructor - nothing installed here at all.

  int frames = 16;
  auto auxA = silentMono(frames);
  auto auxB = silentMono(frames);
  bus.process(auxA, auxB, frames);

  auto & out = bus.getBusAmbisonic();
  for (int c = 0; c < 4; c++) {
    for (int i = 0; i < frames; i++) {
      CHECK_NEAR(out.getChannelData(c)[i], 0.0f, 1e-6f);
    }
  }
}

TEST(send_bus_processor_haze_in_slot_b_reaches_every_ambisonic_channel) {
  // End-to-end: a real Haze instance (default "glue" preset - diffusion
  // 1.0), installed in slot B of a real SendBusProcessor at full 3rd
  // order, fed sustained pink noise - every one of the 16 regular
  // channels should carry real energy by the time the pre-delay has
  // filled and settled. The diffuse encoder's own math (decorrelation,
  // order weighting, taper) already has dedicated coverage in
  // AmbisonicDiffuseEncoderTests.cpp - this test only checks that
  // installing Haze in a slot actually wires it through the real
  // process()/encodeDirect() path, not a re-check of that math.
  ChannelConfiguration config(44100, 3); // 16 channels
  SendBusProcessor bus(config);
  bus.setSlotEffect(SendBusProcessor::kSlotB, make_unique<Haze>(config.getAudioOutSampleRate()));

  NoiseGenerator noise(0x1234);
  PinkNoiseFilter pink;
  int block = 64;
  // Comfortably past the longest possible pre-delay (40ms ~= 1764 samples
  // at 44.1kHz) plus filter/oversampler settle.
  int totalFrames = 8192;

  AudioBuffer lastOut;
  for (int offset = 0; offset < totalFrames; offset += block) {
    AudioBuffer auxA(1, block);
    auxA.zero();
    AudioBuffer auxB(1, block);
    auto auxBData = auxB.getChannelData(0);
    for (int i = 0; i < block; i++) auxBData[i] = 0.5f * pink.process(noise.next());

    bus.process(auxA, auxB, block);
    lastOut = bus.getBusAmbisonic();
  }

  CHECK(lastOut.numberOfChannels() == 16);
  for (int c = 0; c < 16; c++) {
    double energy = 0.0;
    auto data = lastOut.getChannelData(c);
    for (int i = 0; i < block; i++) energy += static_cast<double>(data[i]) * data[i];
    CHECK(energy > 0.0);
  }
}
