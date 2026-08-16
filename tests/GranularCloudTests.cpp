#include "TestFramework.h"

#include "../src/bus/GranularCloud.h"
#include "../src/state/MemoryParameterSource.h"
#include "../src/dsp/NoiseGenerator.h"

#include <cmath>
#include <vector>

using namespace std;

// Grain scheduling/capture/windowing engine invariants (buffer wrap sync,
// catch-up floor at extreme settings, density/freeze behavior, ...) are
// tested directly against dsp::GranularEngine in tests/GranularEngineTests.cpp
// now that the DSP core lives there - this file only covers what's
// specific to this class: BusEffect integration (chain-send default),
// direction assignment, and presets.

TEST(granular_cloud_chain_send_defaults_to_the_base_class_default) {
  // GranularCloud uses BusEffect's own generic chain-send default (0.3),
  // same as delay - grains are spatial events happening in the same room
  // as everything else on the bus, so a little of that room's reverb
  // coloring them by default is the physically sensible behavior. An
  // earlier version of this file zeroed this out instead, suspecting a
  // continuous grain stream would make a slot A reverb's build-up/smear
  // dominate what's heard - measured against a real render and found not
  // to hold up (see kDefaultChainSend's own comment in
  // bus/GranularCloud.cpp): the extra reverb energy reaches a bounded
  // steady state within about a second, it doesn't run away. The actual
  // "takes several seconds, echoes recent material" reports from that
  // period turned out to be caused by real bugs elsewhere (buffer-wrap
  // desync, an off-by-one, a missing catch-up floor).
  int sampleRate = 44100;
  GranularCloud cloud(sampleRate);
  CHECK_NEAR(cloud.getChainSendLevel(), BusEffect::kDefaultChainSendLevel, 0.0001f);
}

TEST(granular_cloud_directions_stay_within_configured_scatter) {
  int sampleRate = 44100;
  GranularCloud cloud(sampleRate);
  float centerAz = 20.0f, centerEl = -15.0f, scatter = 45.0f;
  cloud.setParameters(40.0f, 40.0f, 0.0f, 0.02f, 5.0f, scatter, centerAz, centerEl, 0.1f);

  NoiseGenerator noise(999u);
  vector<float> input(512);
  for (int block = 0; block < 200; block++) {
    for (auto & s : input) s = noise.next();
    cloud.process(input.data(), static_cast<int>(input.size()));

    for (int g = 0; g < GranularCloud::kMaxSimultaneousGrains; g++) {
      if (!cloud.isGrainActiveForTest(g)) continue;
      auto dir = cloud.getTapDirection(g);
      // Independent per-axis jitter (see computeDirection()'s own comment
      // on why this isn't solid-angle-exact) - each axis independently
      // stays within +-scatter of its center, azimuth wrap notwithstanding.
      float azDiff = fabs(dir.azimuth - centerAz);
      if (azDiff > 180.0f) azDiff = 360.0f - azDiff;
      CHECK(azDiff <= scatter + 0.01f);
      CHECK(dir.elevation >= centerEl - scatter - 0.01f);
      CHECK(dir.elevation <= centerEl + scatter + 0.01f);
    }
  }
  CHECK(cloud.getGrainsTriggeredForTest() > 0);
}

TEST(granular_cloud_direction_only_reassigned_when_a_grain_is_retriggered) {
  // Regression-shaped test for the engine-split's own new mechanism: a
  // slot's direction (computeDirection()) must only be recomputed when
  // dsp::GranularEngine::getGrainGeneration() actually advances for that
  // slot, not on every block a grain happens to still be active in -
  // otherwise a long-lived grain (large grainSize) would visibly drift to
  // a new random direction mid-flight instead of staying pinned for its
  // whole life, breaking the per-tap gain-interpolation contract
  // documented on GranularCloud's own class comment.
  int sampleRate = 44100;
  GranularCloud cloud(sampleRate);
  // One long-lived grain at a time: low density (a full trigger interval,
  // 44100/3 =~ 14700 samples, is comfortably longer than a single 512-frame
  // block below, so the polling loop needs several blocks before the first
  // trigger fires), a grain length (180ms =~ 7900 samples) spanning several
  // more blocks after that.
  cloud.setParameters(180.0f, 3.0f, 0.0f, 0.0f, 0.0f, 90.0f, 0.0f, 0.0f, 0.0f);

  NoiseGenerator noise(321u);
  vector<float> input(512);

  // Poll until the first grain triggers, then capture its direction.
  int activeSlot = -1;
  for (int block = 0; block < 100 && activeSlot < 0; block++) {
    for (auto & s : input) s = noise.next();
    cloud.process(input.data(), static_cast<int>(input.size()));
    for (int g = 0; g < GranularCloud::kMaxSimultaneousGrains; g++) {
      if (cloud.isGrainActiveForTest(g)) { activeSlot = g; break; }
    }
  }
  CHECK(activeSlot >= 0);
  auto firstDirection = cloud.getTapDirection(activeSlot);

  // Keep processing while that same slot stays active - its direction
  // must not change.
  for (int block = 0; block < 10 && cloud.isGrainActiveForTest(activeSlot); block++) {
    for (auto & s : input) s = noise.next();
    cloud.process(input.data(), static_cast<int>(input.size()));
    if (!cloud.isGrainActiveForTest(activeSlot)) break;
    auto dir = cloud.getTapDirection(activeSlot);
    CHECK_NEAR(dir.azimuth, firstDirection.azimuth, 0.0001f);
    CHECK_NEAR(dir.elevation, firstDirection.elevation, 0.0001f);
  }
}

TEST(granular_cloud_default_preset_matches_compiled_defaults) {
  // A bare, freshly constructed GranularCloud already reports
  // GranularPreset::DEFAULT - see GranularCloud.h's own doc comment on
  // why this is a real, named, described preset rather than just an
  // unnamed fallback.
  int sampleRate = 44100;
  GranularCloud cloud(sampleRate);
  CHECK(cloud.getPreset() == GranularPreset::DEFAULT);
}

TEST(granular_cloud_unrecognized_preset_text_falls_back_to_default) {
  int sampleRate = 44100;
  GranularCloud cloud(sampleRate);
  MemoryParameterSource input;
  input.set("preset", string("not-a-real-preset"));
  cloud.loadParameters(input);
  CHECK(cloud.getPreset() == GranularPreset::DEFAULT);
}

TEST(granular_cloud_named_preset_changes_parameters_from_default) {
  int sampleRate = 44100;
  GranularCloud defaultCloud(sampleRate);

  GranularCloud cloud(sampleRate);
  MemoryParameterSource input;
  input.set("preset", string("glitch"));
  cloud.loadParameters(input);

  CHECK(cloud.getPreset() == GranularPreset::GLITCH);
  // Glitch is tuned to be clearly, deliberately different from the
  // default preset on (at least) grain size and pitch scatter - a loose
  // sanity check that the preset actually took effect, not an exact
  // numeric pin (see bus/GranularCloud.cpp's presetValues() for the
  // authoritative numbers).
  CHECK(cloud.getGrainSizeMs() != defaultCloud.getGrainSizeMs());
  CHECK(cloud.getPitchScatter() != defaultCloud.getPitchScatter());
}

TEST(granular_cloud_explicit_attribute_overrides_preset) {
  int sampleRate = 44100;
  GranularCloud cloud(sampleRate);
  MemoryParameterSource input;
  input.set("preset", string("cloud"));
  input.set("density", 99.0f);
  cloud.loadParameters(input);

  CHECK(cloud.getPreset() == GranularPreset::CLOUD);
  CHECK_NEAR(cloud.getDensity(), 99.0f, 0.001f);
}

TEST(granular_cloud_preset_alone_round_trips_without_explicit_numeric_attributes) {
  // Loading a named preset with no further overrides, then saving, should
  // write only "preset" (plus whatever BusEffect::storeParameters() always
  // writes) - not every individual numeric attribute too - matching this
  // class's own deviation-only convention (see storeParameters()'s doc
  // comment).
  int sampleRate = 44100;
  GranularCloud cloud(sampleRate);
  MemoryParameterSource input;
  input.set("preset", string("shimmer"));
  cloud.loadParameters(input);

  MemoryParameterSource output;
  cloud.storeParameters(output);

  CHECK(output.getText("preset", "") == "shimmer");
  CHECK(!output.has("grainSize"));
  CHECK(!output.has("density"));
  CHECK(!output.has("scanPosition"));
  CHECK(!output.has("scanJitter"));
  CHECK(!output.has("pitchScatter"));
  CHECK(!output.has("directionScatter"));
  CHECK(!output.has("azimuth"));
  CHECK(!output.has("elevation"));
  CHECK(!output.has("amplitudeJitter"));
}

TEST(granular_cloud_default_preset_round_trips_silently) {
  // GranularPreset::DEFAULT's to_string() is "" (GranularCloud.h) - a
  // fully-default instance must therefore write no "preset" attribute at
  // all, the same as every other implicit-default attribute in this
  // codebase, not an explicit preset="default".
  int sampleRate = 44100;
  GranularCloud cloud(sampleRate);

  MemoryParameterSource output;
  cloud.storeParameters(output);

  CHECK(!output.has("preset"));
  CHECK(output.isEmpty());
}

TEST(granular_cloud_preset_plus_override_round_trips_both) {
  // A preset with one deliberately-overridden attribute on top must save
  // both the preset name and just that one deviating attribute, not
  // silently drop the override.
  int sampleRate = 44100;
  GranularCloud cloud(sampleRate);
  MemoryParameterSource input;
  input.set("preset", string("wash"));
  // 20/sec at wash's own 180ms grain size is overlap 3.6, comfortably
  // above the engine's overlap floor (2.5) - clear of that floor is
  // deliberate here, so this test stays about the deviation-only
  // round-trip mechanism, not about interacting with a second one.
  input.set("density", 20.0f);
  cloud.loadParameters(input);

  MemoryParameterSource output;
  cloud.storeParameters(output);

  CHECK(output.getText("preset", "") == "wash");
  CHECK_NEAR(output.getFloat("density", -1.0f), 20.0f, 0.001f);
  CHECK(!output.has("grainSize"));
  CHECK(!output.has("scanJitter"));
}
