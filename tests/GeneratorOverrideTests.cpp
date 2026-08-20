#include "TestFramework.h"

#include "Sf2Fixture.h"
#include "../src/instruments/SoundFont.h"
#include "../src/ambisonic/ChannelConfiguration.h"
#include "../src/ambisonic/SphericalPosition.h"
#include "../src/model/SendLevels.h"
#include "../src/audio/AudioBuffer.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <unordered_map>

using namespace std;
using namespace sf2fixture;

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

// Exercises Instrument::cloneWithOverrides()/SoundFontInstrument's own
// generator-override support directly against the public SoundFont/
// Instrument API - the same synthetic .sf2 fixture InstrumentResolverTests.cpp
// uses, not a real installed font, so these are hermetic. GenericInstrument's
// own load/save/round-trip coverage lives in SongTests.cpp instead (it needs
// a full Song, not just an Instrument).

namespace {

// Sum of |sample| over the Main channel - a coarse output-level proxy.
// The fixture sample is a pure sine tone (~454 Hz at this font's 44100
// sample rate - see Sf2Fixture.h's own comment), so an extreme-enough
// cutoff attenuates the fundamental itself, not just harmonics that a pure
// tone doesn't have - which is what makes a plain level check meaningful
// here, unlike a real multi-harmonic instrument where only spectral
// content above the cutoff would move. The real "does this darken a real
// piano's upper partials" claim is the plan's own manual listening check,
// not something this synthetic single-tone fixture can stand in for.
float outputLevel(const AudioBuffer & buf) {
  if (!buf.hasChannel(Channel::Main)) return -1.0f;
  auto n = buf.numberOfFrames();
  auto data = buf.getChannelData(0);
  float total = 0.0f;
  for (int i = 0; i < n; i++) total += std::fabs(data[i]);
  return total;
}

AudioBuffer renderOneNote(const Instrument & instrument, const ChannelConfiguration & config) {
  auto voice = instrument.playNote(config, SphericalPosition{}, frequencyForMidiKey(60), 1.0f, 0.8f, 60, SendLevels{});
  return voice->render(4096);
}

// Same as renderOneNote(), but at an arbitrary MIDI key - needed for
// keynumToVolEnvDecay's sign-convention test below, which compares the same
// override applied at two different note numbers.
AudioBuffer renderOneNoteAtKey(const Instrument & instrument, const ChannelConfiguration & config, int key) {
  auto voice = instrument.playNote(config, SphericalPosition{}, frequencyForMidiKey(key), 1.0f, 0.8f, key, SendLevels{});
  return voice->render(4096);
}

bool sameAudio(const AudioBuffer & a, const AudioBuffer & b) {
  if (a.numberOfFrames() != b.numberOfFrames()) return false;
  if (a.hasChannel(Channel::Main) != b.hasChannel(Channel::Main)) return false;
  if (!a.hasChannel(Channel::Main)) return true;
  auto n = a.numberOfFrames();
  auto da = a.getChannelData(0), db = b.getChannelData(0);
  for (int i = 0; i < n; i++) if (da[i] != db[i]) return false;
  return true;
}

} // namespace

TEST(clone_with_overrides_returns_a_distinct_instrument) {
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "generator_clone_identity.sf2").string();
  writeMinimalSf2(path, { { "Test", 0, {}, {} } });

  SoundFont sf(path);
  auto base = sf.createInstrument(0, "test.instrument");
  auto clone = base->cloneWithOverrides({ { SF2Generator::InitialFilterFc, 9000.0f } });

  CHECK(clone != nullptr);
  CHECK(clone.get() != base.get());
  CHECK(clone->getName() == base->getName()); // display identity carries over
}

TEST(clone_with_no_overrides_still_works_and_matches_the_original) {
  // Not the codepath GenericInstrument::prepare() actually takes (it skips
  // cloning entirely when generator_overrides_ is empty), but
  // cloneWithOverrides({}) should still be a safe, correct no-op if called.
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "generator_clone_empty.sf2").string();
  writeMinimalSf2(path, { { "Test", 0, {}, {} } });

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto base = sf.createInstrument(0, "test.instrument");
  auto clone = base->cloneWithOverrides({});
  CHECK(clone != nullptr);

  CHECK(sameAudio(renderOneNote(*base, config), renderOneNote(*clone, config)));
}

TEST(override_audibly_lowers_output_once_cutoff_drops_below_the_fundamental) {
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "generator_audible.sf2").string();
  // No generator 8 in the fixture - initialFilterFc defaults to the SF2
  // spec's fully-open 13500 (see tsf_region::clear()), so the baseline
  // render is unfiltered.
  writeMinimalSf2(path, { { "Test", 0, {}, {} } });

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto base = sf.createInstrument(0, "test.instrument");
  auto open_level = outputLevel(renderOneNote(*base, config));

  // 1500 cents (~19 Hz) is far below the fixture's ~454 Hz sine, so this
  // attenuates the note's own fundamental, not just absent harmonics.
  auto darkened = base->cloneWithOverrides({ { SF2Generator::InitialFilterFc, 1500.0f } });
  CHECK(darkened != nullptr);
  auto darkened_level = outputLevel(renderOneNote(*darkened, config));

  CHECK(open_level > 0.0f);
  CHECK(darkened_level < open_level * 0.5f);
}

TEST(override_below_spec_minimum_clamps_to_1500_cents) {
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "generator_clamp_low.sf2").string();
  writeMinimalSf2(path, { { "Test", 0, {}, {} } });

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto base = sf.createInstrument(0, "test.instrument");

  auto below_min = base->cloneWithOverrides({ { SF2Generator::InitialFilterFc, 500.0f } });   // spec min is 1500
  auto exactly_min = base->cloneWithOverrides({ { SF2Generator::InitialFilterFc, 1500.0f } });
  CHECK(below_min != nullptr);
  CHECK(exactly_min != nullptr);

  CHECK(sameAudio(renderOneNote(*below_min, config), renderOneNote(*exactly_min, config)));
}

TEST(override_above_spec_maximum_clamps_to_13500_cents) {
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "generator_clamp_high.sf2").string();
  writeMinimalSf2(path, { { "Test", 0, {}, {} } });

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto base = sf.createInstrument(0, "test.instrument");

  auto above_max = base->cloneWithOverrides({ { SF2Generator::InitialFilterFc, 99999.0f } }); // spec max is 13500
  auto exactly_max = base->cloneWithOverrides({ { SF2Generator::InitialFilterFc, 13500.0f } });
  CHECK(above_max != nullptr);
  CHECK(exactly_max != nullptr);

  CHECK(sameAudio(renderOneNote(*above_max, config), renderOneNote(*exactly_max, config)));

  // Also matches the un-cloned original, whose initialFilterFc defaults to
  // 13500 in this fixture (no generator 8 authored) - a clamped-to-max
  // override should be indistinguishable from no override at all here.
  CHECK(sameAudio(renderOneNote(*base, config), renderOneNote(*exactly_max, config)));
}

TEST(unrecognized_generator_id_in_the_override_map_is_silently_ignored) {
  // Only SoundFontVoice's effectiveInitialFilterFc() ever reads id 8 out of
  // this map - any other id (e.g. a future generator this backend doesn't
  // apply yet) must not crash or change anything, matching the "backend
  // ignores unhandled generators" contract.
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "generator_unrecognized_id.sf2").string();
  writeMinimalSf2(path, { { "Test", 0, {}, {} } });

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto base = sf.createInstrument(0, "test.instrument");
  auto clone = base->cloneWithOverrides({ { static_cast<SF2Generator>(9999), 42.0f } }); // not a generator this backend reads
  CHECK(clone != nullptr);

  CHECK(sameAudio(renderOneNote(*base, config), renderOneNote(*clone, config)));
}

// --- Volume-envelope generators (33-40) ---

TEST(delay_vol_env_override_above_ceiling_clamps_to_5000) {
  // delayVolEnv (id 33) tops out at 5000 timecents in SF2GeneratorTable.h -
  // a different ceiling than attackVolEnv/decayVolEnv/releaseVolEnv's 8000
  // (see the next test), so this specifically exercises delayVolEnv's own
  // narrower range rather than a shared constant.
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "generator_delay_clamp.sf2").string();
  writeMinimalSf2(path, { { "Test", 0, {}, {} } });

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto base = sf.createInstrument(0, "test.instrument");

  auto above_max = base->cloneWithOverrides({ { SF2Generator::DelayVolEnv, 99999.0f } }); // ceiling is 5000
  auto exactly_max = base->cloneWithOverrides({ { SF2Generator::DelayVolEnv, 5000.0f } });
  CHECK(above_max != nullptr);
  CHECK(exactly_max != nullptr);

  CHECK(sameAudio(renderOneNote(*above_max, config), renderOneNote(*exactly_max, config)));
}

TEST(attack_vol_env_override_above_ceiling_clamps_to_8000) {
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "generator_attack_clamp.sf2").string();
  writeMinimalSf2(path, { { "Test", 0, {}, {} } });

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto base = sf.createInstrument(0, "test.instrument");

  auto above_max = base->cloneWithOverrides({ { SF2Generator::AttackVolEnv, 99999.0f } }); // ceiling is 8000
  auto exactly_max = base->cloneWithOverrides({ { SF2Generator::AttackVolEnv, 8000.0f } });
  CHECK(above_max != nullptr);
  CHECK(exactly_max != nullptr);

  CHECK(sameAudio(renderOneNote(*above_max, config), renderOneNote(*exactly_max, config)));
}

TEST(sustain_vol_env_override_lowers_sustained_output_level) {
  // The fixture's delay/attack/hold/decay all default to -12000 timecents
  // (pinned to 0 seconds by tsf_timecents2SecsPinned()), so an unoverridden
  // voice reaches SUSTAIN immediately at full level (sustain_ defaults to
  // 0 centibels of attenuation = unity gain) - no decay curve to wait out,
  // which makes a plain output-level comparison meaningful right from the
  // first rendered block.
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "generator_sustain_audible.sf2").string();
  writeMinimalSf2(path, { { "Test", 0, {}, {} } });

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto base = sf.createInstrument(0, "test.instrument");
  auto open_level = outputLevel(renderOneNote(*base, config));

  // 1000 centibels (~-100dB) of attenuation - far enough below the
  // fixture's unattenuated default to be unmistakably quieter, well short
  // of the 1440 (silence) ceiling.
  auto quiet = base->cloneWithOverrides({ { SF2Generator::SustainVolEnv, 1000.0f } });
  CHECK(quiet != nullptr);
  auto quiet_level = outputLevel(renderOneNote(*quiet, config));

  CHECK(open_level > 0.0f);
  CHECK(quiet_level < open_level * 0.01f);
}

TEST(keynum_to_vol_env_decay_makes_low_notes_decay_slower_than_high_notes) {
  // SF2's sign convention (EnvelopeState's own
  // `decay_ *= tsf_timecents2Secsf(keynumToDecay_ * (60 - midiNoteNumber))`):
  // a positive keynumToVolEnvDecay shortens decay for keys above 60 and
  // lengthens it for keys below 60 - matching how a real instrument's
  // upper notes typically die away faster than its lower ones. decayVolEnv
  // (2400 timecents = 4s base decay) and sustainVolEnv (~1000 centibels,
  // effectively silent) are both overridden too, so the decay curve
  // actually has somewhere audible to go before settling.
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "generator_keynum_decay.sf2").string();
  writeMinimalSf2(path, { { "Test", 0, {}, {} } });

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto base = sf.createInstrument(0, "test.instrument");
  auto shaped = base->cloneWithOverrides({
    { SF2Generator::DecayVolEnv, 2400.0f },        // 4s base decay time
    { SF2Generator::SustainVolEnv, 1000.0f },      // ~-100dB, effectively silent once decayed
    { SF2Generator::KeynumToVolEnvDecay, 600.0f },
  });
  CHECK(shaped != nullptr);

  // Key 48 (below 60): decay lengthened to ~256s - at 4096 frames in, still
  // essentially at full level. Key 72 (above 60): decay shortened to
  // ~0.06s - well decayed to the near-silent sustain by 4096 frames.
  auto low_level = outputLevel(renderOneNoteAtKey(*shaped, config, 48));
  auto high_level = outputLevel(renderOneNoteAtKey(*shaped, config, 72));

  CHECK(low_level > 0.0f);
  CHECK(high_level < low_level * 0.1f);
}
