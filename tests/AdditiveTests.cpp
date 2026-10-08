#include "TestFramework.h"

#include "../src/model/Song.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/GenericInstrument.h"
#include "../src/state/MemoryParameterSource.h"
#include <filesystem>
#include "../src/audio/OfflineRenderer.h"
#include "../src/ambisonic/ChannelConfiguration.h"

#include "../src/instruments/Additive.h"
#include "../src/instruments/AdditiveModel.h"
#include "../src/instruments/Tuning.h"
#include "../src/model/SendLevels.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#ifndef TESTS_FIXTURES_DIR
#define TESTS_FIXTURES_DIR "."
#endif

namespace {
bool hasNonFiniteSample(const OfflineRenderResult & result) {
  for (auto v : result.interleaved) {
    if (!std::isfinite(v)) return true;
  }
  return false;
}

float peakAbs(const OfflineRenderResult & result) {
  float peak = 0.0f;
  for (auto v : result.interleaved) peak = std::max(peak, std::fabs(v));
  return peak;
}
}

// Loads tests/fixtures/additive_note.xml (<envelope><additive
// preset="struck-string"/></envelope>, one note) through the same
// Song::open()/InstrumentProvider/renderSongOffline() path RenderTests.cpp
// uses for its own fixture-driven smoke tests, and asserts the render is
// finite and actually produces audible output - the basic "the new element
// is wired all the way through the real signal graph" check.
TEST(render_additive_struck_string_note_is_audible_and_finite) {
  InstrumentProvider provider; // no SoundFont: fixture only uses <additive>
  Song song;
  bool ok = song.open(std::string(TESTS_FIXTURES_DIR) + "/additive_note.xml", provider);
  CHECK(ok);

  ChannelConfiguration config(44100, 1);
  auto result = renderSongOffline(song, config);

  CHECK(!hasNonFiniteSample(result));
  CHECK(peakAbs(result) > 1e-4f);
}

TEST(generic_instrument_shows_the_soundfont_preset_name_not_its_path) {
  const char * font = "/usr/share/sounds/sf2/FluidR3_GM.sf2";
  if (!std::filesystem::exists(font)) return;
  InstrumentProvider provider;
  provider.loadSoundFont(font);
  MemoryParameterSource params;
  params.set("from", std::string("bass.electric.pick"));
  GenericInstrument instrument;
  instrument.loadParameters(params);
  instrument.prepare(provider);
  CHECK(instrument.getDisplayName() == "Picked Bass");
}

namespace {
constexpr float kMiddleC = 261.63f;
const int kEdos[] = {12, 19, 31, 53};

float centsOf(float ratio) { return 1200.0f * std::log2(ratio); }

// Steps per octave E of the tuning and the tuned ratio of a just interval
// p:q, as the nearest step.
float tunedIntervalRatio(int edo, int p, int q) {
  return std::exp2(std::round(static_cast<float>(edo) * std::log2(static_cast<float>(p) / static_cast<float>(q))) / static_cast<float>(edo));
}
} // namespace

TEST(additive_every_partial_is_on_a_tuning_step) {
  for (int edo : kEdos) {
    for (int n = 1; n <= 40; n++) {
      float ratio = additivePartialRatio(n, 40, 0.0f, edo, true);
      float steps = static_cast<float>(edo) * std::log2(ratio);
      CHECK_NEAR(steps, std::round(steps), 1e-3f);
      // The nearest step to the true harmonic.
      CHECK(std::fabs(steps - static_cast<float>(edo) * std::log2(static_cast<float>(n))) <= 0.5f + 1e-3f);
    }
  }
}

// Regression: a stretched partial once landed near the third harmonic. No
// partial may fall more than half a step below its harmonic.
TEST(additive_partial_n_is_never_far_below_the_harmonic) {
  for (int edo : kEdos) {
    for (float stretch : {0.0f, 0.001f, 0.01f}) {
      for (int n = 1; n <= 64; n++) {
        // Within half a step of the harmonic, never the thousands of cents off.
        float off = centsOf(additivePartialRatio(n, 64, stretch, edo, true) / static_cast<float>(n));
        CHECK(off >= -(600.0f / static_cast<float>(edo)) - 0.01f);
      }
    }
  }
}

TEST(additive_stretch_is_bounded_and_leaves_the_fundamental_alone) {
  for (int edo : kEdos) {
    for (float stretch : {0.001f, 0.003f}) {
      // (n-1)*stretch over a grid position that can sit half a step below n.
      float limit_cents = 1200.0f * std::log2(1.0f + stretch * std::exp2(0.5f / static_cast<float>(edo)));
      CHECK(additivePartialRatio(1, 64, stretch, edo, true) == 1.0f);
      for (int n = 2; n <= 64; n++) {
        float shift = centsOf(additivePartialRatio(n, 64, stretch, edo, true) / additivePartialRatio(n, 64, 0.0f, edo, true));
        CHECK(shift > 0.0f);
        CHECK(shift <= limit_cents + 1e-3f);
      }
    }
  }
}

// Shared partials of a tuned interval (the root's pk with the upper note's
// qk) coincide on the grid, so what is left is the stretch term: exactly
// stretch * f_root * ((pk - 1) - r * (qk - 1)).
TEST(additive_septimal_shared_partials_meet) {
  struct Interval {
    int p, q;
  };
  const Interval intervals[] = {{7, 6}, {8, 7}, {7, 4}, {12, 7}};
  const float stretch = 0.001f;
  for (int edo : kEdos) {
    for (auto iv : intervals) {
      float r = tunedIntervalRatio(edo, iv.p, iv.q);
      float root = kMiddleC, upper = kMiddleC * r;
      for (int k = 1; iv.p * k <= 24; k++) {
        int pk = iv.p * k, qk = iv.q * k;
        float flat_a = root * additivePartialRatio(pk, 64, 0.0f, edo, true);
        float flat_b = upper * additivePartialRatio(qk, 64, 0.0f, edo, true);
        CHECK(std::fabs(centsOf(flat_a / flat_b)) < 1e-3f);

        float a = root * additivePartialRatio(pk, 64, stretch, edo, true);
        float b = upper * additivePartialRatio(qk, 64, stretch, edo, true);
        float expected = stretch * root * (static_cast<float>(pk - 1) - r * static_cast<float>(qk - 1));
        CHECK_NEAR(a - b, expected, 5e-3f);
        CHECK(std::fabs(a - b) <= 2.0f * stretch * root);
      }
    }
  }
}

TEST(additive_31edo_partial_3_is_5_2_cents_flat) {
  float ratio = additivePartialRatio(3, 28, 0.0f, 31, true);
  CHECK_NEAR(ratio, std::exp2(49.0f / 31.0f), 1e-5f);
  CHECK_NEAR(centsOf(ratio / 3.0f), -5.18f, 0.01f);
}

TEST(additive_tuning_matching_off_gives_plain_harmonics_plus_stretch) {
  for (int n = 1; n <= 30; n++) {
    CHECK_NEAR(additivePartialRatio(n, 30, 0.002f, 31, false), static_cast<float>(n) + static_cast<float>(n - 1) * 0.002f, 1e-5f);
    CHECK_NEAR(additivePartialRatio(n, 30, 0.0f, 0, true), static_cast<float>(n), 1e-5f);
  }
}

TEST(additive_unison_strings_are_centred_and_about_one_cent_apart) {
  CHECK(unisonOffsetCents(0, 1, 1.0f, NoteCoordinate(1, 2, 3)) == 0.0f);
  for (int strings : {2, 3}) {
    for (int row = 0; row < 20; row++) {
      NoteCoordinate coord(1, row, 0);
      float sum = 0.0f;
      std::vector<float> offsets;
      for (int s = 0; s < strings; s++) {
        offsets.push_back(unisonOffsetCents(s, strings, 1.0f, coord));
        sum += offsets.back();
      }
      CHECK(std::fabs(sum / static_cast<float>(strings)) <= 0.15f);
      for (int s = 1; s < strings; s++) {
        float spacing = offsets[static_cast<size_t>(s)] - offsets[static_cast<size_t>(s - 1)];
        CHECK(spacing >= 0.7f && spacing <= 1.3f);
      }
    }
  }
}

TEST(additive_partial_specs_group_by_string_and_skip_nyquist) {
  AdditiveModelParams params;
  params.partials = 28;
  params.unison_voices = 3;
  NoteContext note{kMiddleC, 0.8f, 44100.0f, 31, NoteCoordinate(0, 4, 0)};
  auto specs = buildPartialSpecs(params, note);
  CHECK(specs.size() == 3 * 28);
  int per_group[3] = {0, 0, 0};
  for (const auto & p : specs) {
    CHECK(p.group >= 0 && p.group < 3);
    CHECK(p.frequency_hz < 22050.0f);
    CHECK(p.alpha > 0.0f);
    per_group[p.group]++;
  }
  CHECK(per_group[0] == 28 && per_group[1] == 28 && per_group[2] == 28);

  note.frequency = 3000.0f; // partials 8 and up are past Nyquist
  CHECK(buildPartialSpecs(params, note).size() == 3 * 7);
}

TEST(additive_key_position_rises_with_the_key_and_strings_are_symmetric) {
  CHECK_NEAR(keyboardPosition(kMiddleC), 0.0f, 1e-6f);
  float previous = -1.0f;
  for (float f = 27.5f; f < 4200.0f; f *= 1.06f) {
    float position = keyboardPosition(f);
    CHECK(position >= previous);
    CHECK(position >= -0.5f && position <= 0.5f);
    previous = position;
  }
  CHECK(keyboardPosition(27.5f) < -0.4f);
  CHECK(keyboardPosition(4186.0f) > 0.4f);

  CHECK(stringAzimuthOffsetDeg(0, 1, 5.0f) == 0.0f);
  CHECK(stringAzimuthOffsetDeg(1, 3, 5.0f) == 0.0f);
  CHECK_NEAR(stringAzimuthOffsetDeg(0, 3, 5.0f), -stringAzimuthOffsetDeg(2, 3, 5.0f), 1e-6f);
  CHECK_NEAR(stringAzimuthOffsetDeg(0, 2, 4.0f), -2.0f, 1e-6f);
}

// With a keyboard spread the bass and treble land on opposite sides (the
// sign of Y against W), and with none they are both centred.
TEST(additive_keyboard_spread_places_bass_and_treble_on_opposite_sides) {
  auto side = [](float spread, int note_value) {
    Additive additive;
    MemoryParameterSource params;
    params.set("preset", std::string("piano"));
    params.set("keyboardSpread", spread);
    params.set("attackNoiseLevel", 0.0f);
    additive.loadParameters(params);
    ChannelConfiguration config(44100, 1);
    SphericalPosition position;
    position.distance = 1.0f;
    auto voice = additive.playNote(config, position, Tuning::EDO12, 1.0f, 1.0f, note_value, SendLevels{}, NoteCoordinate(0, 0, 0));
    auto data = voice->render(1024);
    const float * w = data.getChannelData(0);
    const float * y = data.getChannelData(1);
    float yw = 0.0f, ww = 0.0f;
    for (int i = 0; i < 1024; i++) {
      yw += y[i] * w[i];
      ww += w[i] * w[i];
    }
    return yw / ww;
  };
  CHECK(std::fabs(side(0.0f, 36)) < 1e-4f);
  CHECK(std::fabs(side(0.0f, 96)) < 1e-4f);
  float low = side(60.0f, 36), high = side(60.0f, 96);
  CHECK(low * high < 0.0f);
  CHECK(std::fabs(low) > 0.05f && std::fabs(high) > 0.05f);
}

// Timing of the <additive> voice chain (bank plus ambisonic encode), printed
// to stderr and never asserted on. Runs only with SYNTH_TIMING set;
// SYNTH_TIMING_PRESET picks the preset (default "piano").
TEST(additive_voice_timing) {
  if (!std::getenv("SYNTH_TIMING")) return;
  const char * preset_env = std::getenv("SYNTH_TIMING_PRESET");
  std::string preset = preset_env ? preset_env : "piano";

  Additive additive;
  MemoryParameterSource params;
  params.set("preset", preset);
  additive.loadParameters(params);

  ChannelConfiguration config(48000, 3);
  SphericalPosition position;
  position.distance = 1.0f;
  SendLevels sends;
  constexpr int kFrames = 1024, kVoices = 32, kRounds = 20;
  auto now = []() { return std::chrono::steady_clock::now(); };
  auto micros = [](auto a, auto b) { return std::chrono::duration<double, std::micro>(b - a).count(); };

  // One fresh voice: note-on construction, then the first block.
  double on_us = 0.0, first_us = 0.0;
  for (int r = 0; r < kRounds; r++) {
    auto t0 = now();
    auto voice = additive.playNote(config, position, Tuning::EDO12, 1.0f, 1.0f, 60, sends, NoteCoordinate(0, r, 0));
    auto t1 = now();
    voice->render(kFrames);
    auto t2 = now();
    on_us += micros(t0, t1);
    first_us += micros(t1, t2);
  }

  // 32 simultaneous voices spread over the keyboard: first block, then the
  // block 0.5 s in (about 23 blocks later).
  double chord_on_us = 0.0, chord_first_us = 0.0, chord_later_us = 0.0;
  for (int r = 0; r < kRounds; r++) {
    std::vector<std::unique_ptr<VoiceState>> voices;
    auto t0 = now();
    for (int v = 0; v < kVoices; v++) {
      voices.push_back(additive.playNote(config, position, Tuning::EDO12, 1.0f, 1.0f, 36 + v * 2, sends, NoteCoordinate(0, r, v)));
    }
    auto t1 = now();
    for (auto & voice : voices) voice->render(kFrames);
    auto t2 = now();
    for (int b = 0; b < 22; b++)
      for (auto & voice : voices) voice->render(kFrames);
    auto t3 = now();
    for (auto & voice : voices) voice->render(kFrames);
    auto t4 = now();
    chord_on_us += micros(t0, t1);
    chord_first_us += micros(t1, t2);
    chord_later_us += micros(t3, t4);
    (void)t3;
  }

  std::fprintf(stderr,
               "additive timing (%s, %d frames @48 kHz, budget %.0f us/block):\n"
               "  one voice: note-on %.1f us, first block %.1f us\n"
               "  %d voices: note-on %.1f us total, first block %.1f us total, block 24 %.1f us total\n",
               preset.c_str(), kFrames, kFrames / 48000.0 * 1e6,
               on_us / kRounds, first_us / kRounds,
               kVoices, chord_on_us / kRounds, chord_first_us / kRounds, chord_later_us / kRounds);
}
