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
  params.strike = 0.37f; // no mode of the first 28 has a node here
  params.partial_floor_db = -200.0f;
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
    params.set("thump", 0.0f);
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

namespace {
// Spec amplitudes of the first string by partial number (frequency ordered
// ascending equals partial order here), for a note with the given params.
std::vector<PartialSpec> firstString(const std::vector<PartialSpec> & specs) {
  std::vector<PartialSpec> out;
  for (const auto & p : specs)
    if (p.group == 0) out.push_back(p);
  return out;
}

float totalPower(const std::vector<PartialSpec> & specs, int strings) {
  float power = 0.0f;
  for (const auto & p : specs)
    if (p.group == 0) power += p.amplitude * p.amplitude * static_cast<float>(strings * strings);
  return power;
}

NoteContext noteAt(float f0, float velocity = 0.5f, int edo = 12) {
  return NoteContext{f0, velocity, 96000.0f, edo, NoteCoordinate(0, 1, 0)};
}
} // namespace

// Stage 2: the hammer ------------------------------------------------------

TEST(additive_strike_comb_nulls_partial_8_at_one_eighth) {
  AdditiveModelParams params;
  params.partials = 24;
  params.strike = 0.125f;
  params.hammer_cutoff_hz = 1e6f;
  params.partial_floor_db = -120.0f; // a node at a float strike point is ~1e-8, not 0
  auto specs = firstString(buildPartialSpecs(params, noteAt(200.0f)));
  CHECK(specs.size() == 21); // modes 8, 16 and 24 have a node at the strike

  params.strike = 0.1f;
  specs = firstString(buildPartialSpecs(params, noteAt(200.0f)));
  CHECK(specs.size() == 22); // mode 10 has the node now
}

TEST(additive_harder_velocity_is_brighter) {
  AdditiveModelParams params;
  params.partials = 30;
  params.strike = 0.37f;
  params.hammer_velocity = hammerVelocityExponent(3.0f);
  float previous = 0.0f;
  for (float velocity : {0.25f, 0.5f, 1.0f}) {
    auto specs = firstString(buildPartialSpecs(params, noteAt(262.0f, velocity)));
    float high = 0.0f, low = 0.0f;
    for (const auto & p : specs) (p.frequency_hz > 3000.0f ? high : low) += p.amplitude * p.amplitude;
    CHECK(high / low > previous);
    previous = high / low;
  }
}

TEST(additive_hammer_exponent_follows_the_contact_time_formula) {
  CHECK_NEAR(hammerVelocityExponent(2.0f), 1.0f / 3.0f, 1e-6f);
  CHECK_NEAR(hammerVelocityExponent(3.0f), 0.5f, 1e-6f);
  AdditiveModelParams params;
  params.hammer_cutoff_hz = 2000.0f;
  params.hammer_velocity = 0.5f;
  CHECK_NEAR(hammerCutoffHz(params, 261.63f, 0.5f), 2000.0f, 1e-3f);
  CHECK_NEAR(hammerCutoffHz(params, 261.63f, 1.0f), 2000.0f * std::sqrt(2.0f), 1.0f);
}

TEST(additive_hammer_corner_is_in_hz_unless_it_tracks_the_key) {
  AdditiveModelParams params;
  params.partials = 30;
  params.strike = 0.37f;
  params.hammer_cutoff_hz = 1500.0f;
  auto audible = [&](float f0) {
    auto specs = firstString(buildPartialSpecs(params, noteAt(f0)));
    float top = 0.0f;
    for (const auto & p : specs) top = std::max(top, p.amplitude);
    int count = 0;
    for (const auto & p : specs)
      if (p.amplitude > 0.1f * top) count++;
    return count;
  };
  params.hammer_tracking = 0.0f;
  CHECK(audible(65.0f) > audible(130.0f)); // the bass has more partials under the corner
  params.hammer_tracking = 1.0f;
  CHECK(audible(65.0f) == audible(130.0f));
}

TEST(additive_every_note_has_the_same_total_power_before_pruning) {
  AdditiveModelParams params;
  params.partials = 30;
  params.strike = 0.37f;
  params.unison_voices = 3;
  params.partial_floor_db = -300.0f;
  for (float f0 : {65.0f, 262.0f, 523.0f}) {
    for (float velocity : {0.25f, 1.0f}) {
      CHECK_NEAR(totalPower(buildPartialSpecs(params, noteAt(f0, velocity)), 3), 0.25f, 1e-3f);
    }
  }
}

TEST(additive_partial_floor_prunes_and_loses_almost_no_power) {
  AdditiveModelParams params;
  params.partials = 80;
  params.strike = 0.37f;
  params.hammer_cutoff_hz = 150.0f; // a dull hammer: the top of 80 partials is far below the strongest
  params.partial_floor_db = -300.0f;
  auto all = buildPartialSpecs(params, noteAt(65.0f));
  params.partial_floor_db = -40.0f;
  auto some = buildPartialSpecs(params, noteAt(65.0f));
  CHECK(some.size() < all.size());
  params.partial_floor_db = -60.0f;
  auto pruned = buildPartialSpecs(params, noteAt(65.0f));
  CHECK(pruned.size() < all.size());
  // Power of the partials -60 dB leaves out, compared with the whole.
  float whole = 0.0f, kept = 0.0f;
  for (const auto & p : all) whole += p.amplitude * p.amplitude;
  for (const auto & p : pruned) kept += p.amplitude * p.amplitude;
  CHECK(kept / whole > 0.99f);
}

TEST(additive_piano_resonator_count_is_bounded_on_every_key) {
  const auto & preset = getAdditivePreset("piano");
  AdditiveModelParams params;
  params.partials = preset.partials;
  params.unison_voices = preset.unisonVoices;
  params.strike = preset.strike;
  params.hammer_cutoff_hz = preset.hammerCutoff;
  params.hammer_velocity = preset.hammerVelocity;
  params.thump = preset.thump;
  params.body = preset.body;
  for (float f = 32.7f; f < 2100.0f; f *= 1.0595f) {
    auto specs = buildPartialSpecs(params, noteAt(f, 1.0f, 31));
    CHECK(specs.size() <= static_cast<size_t>(preset.unisonVoices * preset.partials) + preset.body.size());
    CHECK(specs.size() >= 20);
  }
}

// Stage 3: decay and thump -------------------------------------------------

TEST(additive_decay_rises_with_frequency_and_falls_with_key) {
  AdditiveModelParams params;
  params.partials = 8;
  params.strike = 0.37f;
  params.decay_a = 0.05f;
  params.decay_b = 1e-4f;
  params.decay_p = 1.5f;
  params.decay_tracking = 0.5f;
  params.tuning_matched = false;
  params.partial_floor_db = -300.0f;
  auto alphaAt = [&](float f0, int n) {
    for (const auto & p : buildPartialSpecs(params, noteAt(f0)))
      if (p.group == 0 && std::fabs(p.frequency_hz - f0 * static_cast<float>(n)) < 1.0f) return p.alpha;
    return -1.0f;
  };
  CHECK(alphaAt(262.0f, 4) > alphaAt(262.0f, 1)); // higher partial, faster
  CHECK(alphaAt(65.5f, 4) < alphaAt(262.0f, 1));  // same frequency, lower key rings longer
}

TEST(additive_unison_strings_have_different_decay_rates) {
  AdditiveModelParams params;
  params.partials = 1;
  params.strike = 0.37f;
  params.unison_voices = 3;
  params.decay_spread = 0.5f;
  params.decay_b = 0.0f; // so the strings' cent-sized frequency offsets don't differ the rates
  params.partial_floor_db = -300.0f;
  float alpha[3] = {0, 0, 0};
  for (const auto & p : buildPartialSpecs(params, noteAt(262.0f))) alpha[p.group] = p.alpha;
  CHECK(alpha[0] < alpha[1] && alpha[1] < alpha[2]);
  CHECK_NEAR(alpha[0] / alpha[1], 0.5f / 1.0f, 1e-3f);
  CHECK_NEAR(alpha[2] / alpha[1], 1.5f, 1e-3f);

  params.decay_spread = 0.0f;
  for (const auto & p : buildPartialSpecs(params, noteAt(262.0f))) alpha[p.group] = p.alpha;
  CHECK(alpha[0] == alpha[1] && alpha[1] == alpha[2]);
}

// Three strings with unequal decay sum to a fast first decay and a slow
// aftersound; one string is a single exponential.
TEST(additive_two_stage_decay) {
  AdditiveModelParams params;
  params.partials = 1;
  params.strike = 0.37f;
  params.decay_a = 1.0f;
  params.decay_b = 0.0f;
  params.decay_spread = 0.6f;
  params.partial_floor_db = -300.0f;
  auto slope = [&](int strings, float t0, float t1) {
    params.unison_voices = strings;
    auto specs = buildPartialSpecs(params, noteAt(262.0f));
    auto level = [&](float t) {
      float sum = 0.0f;
      for (const auto & p : specs) sum += p.amplitude * std::exp(-p.alpha * t);
      return sum;
    };
    return (std::log(level(t1)) - std::log(level(t0))) / (t1 - t0);
  };
  CHECK(slope(3, 0.0f, 0.5f) < 1.5f * slope(3, 4.0f, 4.5f)); // both negative: early is steeper
  CHECK_NEAR(slope(1, 0.0f, 0.5f), slope(1, 4.0f, 4.5f), 1e-3f);
}

TEST(additive_thump_is_fixed_in_hz_short_and_off_without_a_level) {
  AdditiveModelParams params;
  params.partials = 12;
  params.strike = 0.37f;
  params.unison_voices = 2;
  params.thump = 0.3f;
  params.body = {{70.0f, 1.0f, 60.0f}, {120.0f, 0.7f, 50.0f}};
  auto bodyOf = [&](float f0, int edo) {
    std::vector<PartialSpec> out;
    for (const auto & p : buildPartialSpecs(params, noteAt(f0, 0.5f, edo)))
      if (p.group >= 2) out.push_back(p);
    return out;
  };
  auto low = bodyOf(65.0f, 12), high = bodyOf(1046.0f, 31);
  CHECK(low.size() == 2 && high.size() == 2);
  for (size_t i = 0; i < 2; i++) {
    CHECK(low[i].frequency_hz == high[i].frequency_hz);
    CHECK(low[i].group == 2 + static_cast<int>(i));
    CHECK(low[i].amplitude > 0.0f);
    CHECK(std::exp(-low[i].alpha * 0.15f) < 1e-3f); // gone within 150 ms
  }

  params.thump = 0.0f;
  CHECK(bodyOf(65.0f, 12).empty());
}

TEST(additive_body_modes_are_spread_wider_than_the_strings) {
  float lo = 1e9f, hi = -1e9f;
  for (int j = 0; j < 3; j++) {
    float a = bodyAzimuthOffsetDeg(j, 3, 60.0f);
    lo = std::min(lo, a);
    hi = std::max(hi, a);
  }
  CHECK_NEAR(lo, -30.0f, 1e-4f);
  CHECK_NEAR(hi, 30.0f, 1e-4f);
  CHECK(bodyAzimuthOffsetDeg(0, 1, 60.0f) == 0.0f);
  // Neighbouring modes are not neighbours in space.
  CHECK(std::fabs(bodyAzimuthOffsetDeg(0, 4, 60.0f) - bodyAzimuthOffsetDeg(1, 4, 60.0f)) > 20.0f);
  CHECK(hi - lo > 2.0f * stringAzimuthOffsetDeg(2, 3, getAdditivePreset("piano").thumpWidth > 0.0f ? 1.0f : 0.0f));
}

// Stage 4: plucks, modes and presets ----------------------------------------

TEST(additive_pluck_spectrum_follows_one_over_n_squared_with_a_comb) {
  AdditiveModelParams params;
  params.partials = 12;
  params.excitation = Excitation::Pluck;
  params.strike = 0.2f;
  params.pluck_cutoff_hz = 0.0f;
  params.tuning_matched = false;
  params.partial_floor_db = -120.0f;
  auto specs = firstString(buildPartialSpecs(params, noteAt(100.0f)));
  CHECK(specs.size() == 10); // modes 5 and 10 have a node at 1/5
  float reference = 0.0f;
  for (const auto & p : specs) {
    int n = static_cast<int>(std::lround(p.frequency_hz / 100.0f));
    float shape = p.amplitude * static_cast<float>(n * n) / std::fabs(std::sin(static_cast<float>(M_PI) * static_cast<float>(n) * 0.2f));
    if (reference == 0.0f) reference = shape;
    CHECK_NEAR(shape / reference, 1.0f, 1e-3f);
  }
}

TEST(additive_pluck_does_not_brighten_with_velocity) {
  AdditiveModelParams params;
  params.partials = 20;
  params.excitation = Excitation::Pluck;
  params.strike = 0.2f;
  params.pluck_cutoff_hz = 3000.0f;
  auto quiet = buildPartialSpecs(params, noteAt(200.0f, 0.25f));
  auto loud = buildPartialSpecs(params, noteAt(200.0f, 1.0f));
  CHECK(quiet.size() == loud.size());
  for (size_t i = 0; i < quiet.size(); i++) CHECK(quiet[i].amplitude == loud[i].amplitude);
}

TEST(additive_modes_replace_the_harmonic_series) {
  CHECK(parseModeRatios("1 2.756 5.404").size() == 3);
  CHECK(parseModeRatios("").empty());

  AdditiveModelParams params;
  params.partials = 4;
  params.modes = {1.0f, 2.756f, 5.404f, 8.933f};
  params.partial_floor_db = -300.0f;
  for (int edo : {12, 31}) {
    auto specs = buildPartialSpecs(params, noteAt(220.0f, 0.5f, edo));
    CHECK(specs.size() == 4);
    for (size_t k = 0; k < 4; k++) CHECK_NEAR(specs[k].frequency_hz, 220.0f * params.modes[k], 1e-3f);
  }
  params.partials = 2;
  CHECK(buildPartialSpecs(params, noteAt(220.0f)).size() == 2);
}

// The modes of a free-free bar are the roots of cos(x)*cosh(x) = 1.
TEST(additive_bar_preset_ratios_match_the_free_free_bar_roots) {
  auto root = [](double guess) {
    double lo = guess - 0.3, hi = guess + 0.3;
    auto f = [](double x) { return std::cos(x) * std::cosh(x) - 1.0; };
    for (int i = 0; i < 100; i++) {
      double mid = 0.5 * (lo + hi);
      if (f(lo) * f(mid) <= 0.0)
        hi = mid;
      else
        lo = mid;
    }
    return 0.5 * (lo + hi);
  };
  double first = root(4.73);
  auto ratios = parseModeRatios(getAdditivePreset("bar").modes);
  CHECK(ratios.size() == 4);
  CHECK_NEAR(ratios[0], 1.0f, 1e-6f);
  int k = 1;
  for (double guess : {7.853, 10.996, 14.137}) {
    double ratio = std::pow(root(guess) / first, 2.0);
    CHECK_NEAR(static_cast<float>(ratio), ratios[static_cast<size_t>(k)], 1e-3f);
    k++;
  }
}

TEST(additive_every_preset_renders_finite_and_audible) {
  for (const char * name : {"default", "piano", "guitar-nylon", "guitar-steel", "harp", "harpsichord", "bar"}) {
    Additive additive;
    MemoryParameterSource params;
    params.set("preset", std::string(name));
    additive.loadParameters(params);
    ChannelConfiguration config(44100, 1);
    SphericalPosition position;
    position.distance = 1.0f;
    auto voice = additive.playNote(config, position, Tuning::EDO31, 1.0f, 0.8f, 160, SendLevels{}, NoteCoordinate(0, 0, 0));
    float energy = 0.0f;
    for (int block = 0; block < 4; block++) {
      auto data = voice->render(1024);
      const float * w = data.getChannelData(0);
      for (int i = 0; i < 1024; i++) {
        CHECK(std::isfinite(w[i]));
        energy += w[i] * w[i];
      }
    }
    CHECK(energy > 1e-4f);
  }
}

TEST(additive_preset_structure) {
  auto groups = [](const char * name) {
    const auto & preset = getAdditivePreset(name);
    AdditiveModelParams params;
    params.partials = preset.partials;
    params.unison_voices = preset.unisonVoices;
    params.modes = parseModeRatios(preset.modes);
    params.excitation = preset.pluck ? Excitation::Pluck : Excitation::Hammer;
    params.strike = preset.strike;
    params.hammer_cutoff_hz = preset.hammerCutoff;
    params.pluck_cutoff_hz = preset.pluckCutoff;
    params.thump = preset.thump;
    params.body = preset.body;
    int highest = -1;
    for (const auto & p : buildPartialSpecs(params, noteAt(220.0f))) highest = std::max(highest, p.group);
    return highest + 1;
  };
  CHECK(groups("harpsichord") == 2);      // two strings, no body
  CHECK(groups("guitar-nylon") == 1 + 2); // one string, two body modes
  CHECK(groups("default") == 1);
  CHECK(groups("piano") == 3 + 3);
  CHECK(getAdditivePreset("guitar-nylon").pluckCutoff < getAdditivePreset("guitar-steel").pluckCutoff);
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
