#include "TestFramework.h"

#include "../src/dsp/ChorusEngine.h"

#include <cmath>
#include <vector>

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
    AudioBuffer data(1, frames);
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
  AudioBuffer data(2, frames);
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
  AudioBuffer data(2, frames);
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
  // - relied on by the per-track <chorus> effect (effects/Chorus.cpp),
  // which uses decorrelate=false specifically so a hard-panned source's
  // silent side stays silent rather than picking up width invented from
  // nothing.
  ChorusEngine engine(2, 44100, 3, 0.5f, 15.0f, 4.0f, false);
  engine.setMix(1.0f);

  int frames = 512;
  AudioBuffer data(2, frames);
  data.zero();
  for (int i = 0; i < frames; i++) data.getChannelData(0)[i] = sinf(static_cast<float>(i) * 0.1f);
  engine.process(data);

  for (int i = 0; i < frames; i++) {
    CHECK_NEAR(data.getChannelData(1)[i], 0.0f, 1e-6f);
  }
}

TEST(chorus_engine_processes_aux_channels_too) {
  // Aux channels get the same chorus treatment Main does now (the reverb/
  // delay bus should hear the same modulated character the dry signal
  // does) - each with its own persistent, separately-tracked delay-line/
  // LFO state (see ChorusEngine.h's own doc comment on why that state
  // can't just be raw-index-shared with Main's).
  ChorusEngine engine(1, 44100, 3, 2.0f, 15.0f, 4.0f, false);
  engine.setMix(1.0f);

  int frames = 2000;
  AudioBuffer data(0, true, false, frames); // no Main at all, AuxA only
  auto aux = data.getChannel(Channel::AuxA);
  std::vector<float> original(static_cast<size_t>(frames));
  for (int i = 0; i < frames; i++) {
    aux[i] = sinf(static_cast<float>(i) * 0.05f);
    original[static_cast<size_t>(i)] = aux[i];
  }

  engine.process(data);

  double diff_energy = 0.0;
  auto processed = data.getChannel(Channel::AuxA);
  for (int i = 0; i < frames; i++) {
    auto d = processed[i] - original[static_cast<size_t>(i)];
    diff_energy += d * d;
  }
  CHECK(diff_energy > 1e-3);
}

TEST(chorus_engine_main_and_aux_never_cross_mix) {
  // Same "never invent width/content from a sibling channel" guarantee
  // chorus_engine_never_cross_mixes_channels checks between two Main
  // channels, but between Main and Aux's independent state instead.
  ChorusEngine engine(1, 44100, 3, 0.5f, 15.0f, 4.0f, false);
  engine.setMix(1.0f);

  int frames = 512;
  AudioBuffer data(1, true, false, frames); // Main (W) + AuxA
  data.zero();
  for (int i = 0; i < frames; i++) data.getChannelData(0)[i] = sinf(static_cast<float>(i) * 0.1f);
  // AuxA left at 0 (silent) by zero() above.
  engine.process(data);

  auto aux = data.getChannel(Channel::AuxA);
  for (int i = 0; i < frames; i++) {
    CHECK_NEAR(aux[i], 0.0f, 1e-6f);
  }
}
