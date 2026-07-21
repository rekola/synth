#include "TestFramework.h"

#include "../effects/ChorusEngine.h"

#include <cmath>

TEST(chorus_engine_modulation_actually_changes_output) {
  // Compares a real (depth > 0) engine against an otherwise-identical
  // engine with depth = 0 (i.e. no LFO modulation, a plain fixed delay) fed
  // the exact same signal - if depth has no effect, this is a bug (the
  // "modulated" engine would just be a fixed delay wearing a costume).
  // frames is long enough that the ~15ms delay line fills with real signal
  // rather than the still-zero-initialized tail; rate is sped up from the
  // engine's slow chorus-appropriate default so the LFO visibly moves
  // within that many frames.
  int frames = 2000;
  auto make_input = [&]() {
    SampleData data(1, frames);
    for (int i = 0; i < frames; i++) data.getChannelData(0)[i] = sinf(static_cast<float>(i) * 0.05f);
    return data;
  };

  ChorusEngine modulated(1, 44100, 3, 2.0f, 15.0f, 4.0f, false);
  modulated.setMix(1.0f);
  auto data_mod = make_input();
  modulated.process(data_mod);

  ChorusEngine unmodulated(1, 44100, 3, 2.0f, 15.0f, 0.0f, false);
  unmodulated.setMix(1.0f);
  auto data_flat = make_input();
  unmodulated.process(data_flat);

  double diff_energy = 0.0;
  for (int i = 0; i < frames; i++) {
    auto d = data_mod.getChannelData(0)[i] - data_flat.getChannelData(0)[i];
    diff_energy += d * d;
  }
  CHECK(diff_energy > 1e-3);
}

TEST(chorus_engine_decorrelate_false_keeps_duplicated_mono_identical) {
  // decorrelate = false (the per-track effect's mode): both channels use
  // identical voice phases, so feeding them identical content keeps them
  // identical - no width invented where the input had none.
  ChorusEngine engine(2, 44100, 3, 0.5f, 15.0f, 4.0f, false);
  engine.setMix(1.0f);

  int frames = 512;
  SampleData data(2, frames);
  for (int i = 0; i < frames; i++) {
    float v = sinf(static_cast<float>(i) * 0.1f);
    data.getChannelData(0)[i] = v;
    data.getChannelData(1)[i] = v;
  }
  engine.process(data);

  for (int i = 0; i < frames; i++) {
    CHECK_NEAR(data.getChannelData(0)[i], data.getChannelData(1)[i], 1e-5f);
  }
}

TEST(chorus_engine_decorrelate_true_creates_stereo_width) {
  // decorrelate = true (the shared send-bus's mode): channel 1's voices are
  // phase-offset from channel 0's, so two initially identical channels
  // diverge into measurable width.
  ChorusEngine engine(2, 44100, 3, 0.5f, 15.0f, 4.0f, true);
  engine.setMix(1.0f);

  int frames = 512;
  SampleData data(2, frames);
  for (int i = 0; i < frames; i++) {
    float v = sinf(static_cast<float>(i) * 0.1f);
    data.getChannelData(0)[i] = v;
    data.getChannelData(1)[i] = v;
  }
  engine.process(data);

  double diff_energy = 0.0;
  for (int i = 0; i < frames; i++) {
    auto d = data.getChannelData(0)[i] - data.getChannelData(1)[i];
    diff_energy += d * d;
  }
  CHECK(diff_energy > 1e-3);
}

TEST(chorus_engine_never_cross_mixes_channels) {
  // A silent channel must stay silent regardless of a loud sibling channel
  // - the per-track effect's stereo-image-preservation guarantee depends
  // on this (see render_chorus_preserves_stereo_image in RenderTests.cpp).
  ChorusEngine engine(2, 44100, 3, 0.5f, 15.0f, 4.0f, false);
  engine.setMix(1.0f);

  int frames = 512;
  SampleData data(2, frames);
  data.zero();
  for (int i = 0; i < frames; i++) data.getChannelData(0)[i] = sinf(static_cast<float>(i) * 0.1f);
  engine.process(data);

  for (int i = 0; i < frames; i++) {
    CHECK_NEAR(data.getChannelData(1)[i], 0.0f, 1e-6f);
  }
}

TEST(chorus_engine_ignores_send_channels) {
  // The engine's per-channel state is sized once, at construction, from
  // the regular channel count - it must never touch SendA/SendB even if
  // they're present on the SampleData it's given (the same class of bug
  // fixed in ResonantFilter/BiquadFilter/Delay).
  ChorusEngine engine(2, 44100);
  engine.setMix(1.0f);

  int frames = 64;
  SampleData data({ Channel::Left, Channel::Right, Channel::SendA }, frames);
  data.zero();
  for (int i = 0; i < frames; i++) {
    data.getChannelData(0)[i] = 1.0f;
    data.getChannelData(1)[i] = 1.0f;
    data.getChannel(Channel::SendA)[i] = 5.0f;
  }
  engine.process(data);

  for (int i = 0; i < frames; i++) {
    CHECK_NEAR(data.getChannel(Channel::SendA)[i], 5.0f, 1e-6f);
  }
}
