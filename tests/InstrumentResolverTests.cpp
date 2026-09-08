#include "TestFramework.h"

#include "Sf2Fixture.h"
#include "../src/instruments/InstrumentProvider.h"
#include "../src/instruments/SoundFont.h"
#include "../src/instruments/Oscillator.h"

#include <filesystem>
#include <memory>
#include <string>

using namespace std;
using namespace sf2fixture;

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

// Exercises InstrumentProvider::registerPath()/resolvePath() and
// SoundFont::createInstrumentByProgram() directly - the resolver described
// in docs/instrument-paths.md. Uses synthetic .sf2 fixtures (Sf2Fixture.h,
// shared with SF2ModulatorTests.cpp) rather than a real installed font, so
// these are hermetic and don't depend on what's installed on the machine
// running them.

TEST(create_instrument_by_program_finds_the_same_patch_despite_a_bank0_gap) {
  // Font A: bank-0 programs 0,1,2 contiguous - index == program, the
  // (accidentally) easy case. Font B: the same three patches, but with an
  // unrelated preset inserted at program 1, pushing "Lead" (program 2 in
  // both fonts) to a different raw sorted-array index in font B than in
  // font A. createInstrumentByProgram() must still find it by (bank,
  // program), not by position - see its own doc comment.
  vector<PresetSpec> presetsA = {
    { "Piano", 0, {}, {} },
    { "Bass",  1, {}, {} },
    { "Lead",  2, {}, {} },
  };
  vector<PresetSpec> presetsB = {
    { "Piano",  0, {}, {} },
    { "Extra", 1, {}, {} }, // the gap - "Bass" isn't here at all
    { "Bass",   2, {}, {} },
    { "Lead",   3, {}, {} },
  };

  auto pathA = (filesystem::path(TESTS_SCRATCH_DIR) / "resolver_gap_a.sf2").string();
  auto pathB = (filesystem::path(TESTS_SCRATCH_DIR) / "resolver_gap_b.sf2").string();
  writeMinimalSf2(pathA, presetsA);
  writeMinimalSf2(pathB, presetsB);

  SoundFont fontA(pathA);
  SoundFont fontB(pathB);

  // "Lead" is program 2 in font A (array index 2, no gap ahead of it) and
  // program 3 in font B (array index 3, one gap ahead of it) - the same
  // logical patch, different (bank,program), different array position in
  // each file. createInstrumentByProgram(0, 2, ...) on A and
  // createInstrumentByProgram(0, 3, ...) on B should each find "Lead".
  auto leadA = fontA.createInstrumentByProgram(0, 2, "lead.test");
  auto leadB = fontB.createInstrumentByProgram(0, 3, "lead.test");
  CHECK(leadA != nullptr);
  CHECK(leadB != nullptr);

  // Asking font B for program 2 (where "Lead" sits in font A) must NOT
  // return "Lead" - it should return "Bass", proving the lookup is real
  // (bank,program) addressing, not a coincidence of both fonts happening
  // to agree.
  auto bassB = fontB.createInstrumentByProgram(0, 2, "bass.test");
  CHECK(bassB != nullptr);
  CHECK(bassB->getName() == "bass.test");
}

TEST(create_instrument_by_program_returns_nullptr_for_an_absent_program) {
  vector<PresetSpec> presets = { { "Piano", 0, {}, {} } };
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "resolver_absent.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont font(path);
  CHECK(font.createInstrumentByProgram(0, 0, "present") != nullptr);
  CHECK(font.createInstrumentByProgram(0, 1, "absent") == nullptr);   // no program 1 in this font
  CHECK(font.createInstrumentByProgram(128, 0, "absent") == nullptr); // no bank 128 either
}

TEST(register_path_absent_registration_returns_nullptr_not_a_registered_instrument) {
  InstrumentProvider provider;
  CHECK(provider.resolvePath("piano.acoustic.grand") == nullptr);
}

TEST(resolve_path_walks_up_to_a_registered_ancestor) {
  InstrumentProvider provider;
  auto harp = make_shared<Oscillator>(WaveformType::SAW);
  harp->setName("string.plucked.harp");
  provider.registerPath("string.plucked.harp", harp);

  // Nothing is registered at the more specific leaf - walk-up should find
  // the ancestor that IS registered.
  auto resolved = provider.resolvePath("string.plucked.harp.someCustomModel");
  CHECK(resolved == harp);
}

TEST(resolve_path_returns_the_exact_match_over_an_ancestor_when_both_exist) {
  InstrumentProvider provider;
  auto grand = make_shared<Oscillator>(WaveformType::SAW);
  grand->setName("piano.acoustic.grand");
  auto bright = make_shared<Oscillator>(WaveformType::SAW);
  bright->setName("piano.acoustic.grand.bright");
  provider.registerPath("piano.acoustic.grand", grand);
  provider.registerPath("piano.acoustic.grand.bright", bright);

  CHECK(provider.resolvePath("piano.acoustic.grand.bright") == bright);
  CHECK(provider.resolvePath("piano.acoustic.grand") == grand);
}

TEST(resolve_path_redirects_a_bare_root_through_the_defaults_table) {
  InstrumentProvider provider;
  auto tine = make_shared<Oscillator>(WaveformType::SAW);
  tine->setName("piano.electric.tine");
  provider.registerPath("piano.electric.tine", tine);

  // "piano.electric" has no direct registration - only the defaults-table
  // redirect (piano.electric -> piano.electric.tine, per
  // docs/instrument-paths.md) should resolve it.
  CHECK(provider.resolvePath("piano.electric") == tine);
}

TEST(resolve_path_returns_null_when_a_defaults_target_is_itself_unregistered) {
  // Regression test: this used to stack-overflow. "kit" redirects to
  // "kit.standard" in the defaults table, but this bare provider never had
  // loadSoundFont() called on it, so nothing is registered under "kit." at
  // all - resolving either "kit" or "kit.standard" directly must return
  // nullptr cleanly, not recurse back into the same redirect forever.
  InstrumentProvider provider;
  CHECK(provider.resolvePath("kit") == nullptr);
  CHECK(provider.resolvePath("kit.standard") == nullptr);
  CHECK(provider.resolvePath("kit.someUnregisteredKit") == nullptr);
}

TEST(resolve_path_returns_null_for_a_path_with_no_registration_or_default) {
  InstrumentProvider provider;
  CHECK(provider.resolvePath("totally.madeUp.path") == nullptr);
}

TEST(register_path_last_registration_wins_on_collision) {
  InstrumentProvider provider;
  auto first = make_shared<Oscillator>(WaveformType::SAW);
  first->setName("piano.acoustic.grand");
  auto second = make_shared<Oscillator>(WaveformType::SAW);
  second->setName("piano.acoustic.grand");

  provider.registerPath("piano.acoustic.grand", first);
  provider.registerPath("piano.acoustic.grand", second);

  CHECK(provider.resolvePath("piano.acoustic.grand") == second);
}

TEST(try_get_by_literal_name_does_not_fall_back_to_the_default_instrument) {
  InstrumentProvider provider;
  CHECK(provider.tryGetByLiteralName("nothing registered under this string") == nullptr);
}

TEST(load_sound_font_registers_bank_128_kits_under_their_own_kit_paths) {
  // A font carrying two of the nine documented kits (Standard at program 0,
  // Jazz at program 32 - see kGmBank128Table/docs/instrument-paths.md) plus
  // one bank-0 patch, exercising InstrumentProvider::loadSoundFont()'s
  // bank-128 loop end to end, not just registerPath()/resolvePath() in
  // isolation like the tests above.
  vector<PresetSpec> presets = {
    { "Piano", 0, {}, {}, 1, {}, 0 },
    { "StandardKit", 0, {}, {}, 1, {}, 128 },
    { "JazzKit", 32, {}, {}, 1, {}, 128 },
  };
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "resolver_bank128.sf2").string();
  writeMinimalSf2(path, presets);

  InstrumentProvider provider;
  provider.loadSoundFont(path);

  auto standard = provider.resolvePath("kit.standard");
  auto jazz = provider.resolvePath("kit.jazz");
  CHECK(standard != nullptr);
  CHECK(jazz != nullptr);
  CHECK(standard != jazz);

  // Room (program 8) isn't in this font - createInstrumentByProgram()'s
  // nullptr-on-miss contract means it's simply never registered, same as
  // an absent bank-0 program. resolvePath() doesn't return null for it
  // though: with nothing registered at "kit.room" or "kit", it falls
  // through to the defaults-table redirect ("kit" -> "kit.standard"), the
  // same fallback an unregistered leaf under any other defaulted root
  // (e.g. "piano.acoustic.somethingObscure") already gets.
  CHECK(provider.resolvePath("kit.room") == standard);
  // Bare "kit" resolves the same way.
  CHECK(provider.resolvePath("kit") == standard);
}

// getTaxonomyPaths() is what a UI should show as "the" instrument library
// (OutlineView.cpp) - the curated dotted paths registerPath() populated
// (via loadSoundFont()'s bank-0/128 loops), not getInstruments()'s own
// "native:"-namespaced raw SF2 preset names, which loadSoundFont() only
// ever adds as a same-font fallback for a preset with no curated path.
TEST(get_taxonomy_paths_exposes_registered_paths_not_native_names) {
  vector<PresetSpec> presets = {
    { "Piano", 0, {}, {}, 1, {}, 0 }, // program 0 -> "piano.acoustic.grand"
  };
  auto path = (filesystem::path(TESTS_SCRATCH_DIR) / "resolver_taxonomy_paths.sf2").string();
  writeMinimalSf2(path, presets);

  InstrumentProvider provider;
  provider.loadSoundFont(path);

  CHECK(provider.getTaxonomyPaths().count("piano.acoustic.grand") == 1);
  CHECK(provider.getTaxonomyPaths().count("native:Piano") == 0);

  // The native name lands in getInstruments() instead, never in
  // getTaxonomyPaths() - the two accessors are disjoint by construction
  // (registerPath() and addInstrument() write to two separate maps).
  CHECK(provider.getInstruments().count("native:Piano") == 1);
  CHECK(provider.getInstruments().count("piano.acoustic.grand") == 0);
}
