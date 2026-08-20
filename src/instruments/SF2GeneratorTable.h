#ifndef _SF2GENERATORTABLE_H_
#define _SF2GENERATORTABLE_H_

#include <algorithm>
#include <optional>
#include <string_view>

// Name<->id table for the SF2 generators a song can currently override via
// an <instrument>'s <generator name="..." value="..."/> children. SF2's own
// numeric generator ids have no existing name<->id mapping anywhere else in
// this codebase - SoundFont.cpp's genMetas table (the generator-parsing
// table tsf_load_presets() itself uses) is indexed by numeric id only, with
// each generator's name living purely in a same-line comment
// (`// 8 InitialFilterFc`).
//
// Deliberately small - only what some backend actually applies an override
// for belongs here, not the full ~59-generator SF2 spec table. An
// unrecognized name in a song file is preserved unapplied
// (GenericInstrument::unknown_generator_overrides_), never rejected, so
// adding a row here later is purely additive and never a compatibility
// break either direction - an older binary keeps round-tripping a newer
// file's not-yet-recognized generator names losslessly instead of dropping
// them.
//
// Each entry carries its own valid range alongside the id - min/max differ
// per generator (see the table below), so a single shared range would be
// wrong for most of them. Every range here is taken directly from
// SoundFont.cpp's own genMetas table (the ordinary, non-override
// generator-merge clamp the engine already enforces for every preset's own
// authored values - GEN_INT_LIMITFC/GEN_FLOAT_LIMIT12K5K/
// GEN_FLOAT_LIMIT12K8K/GEN_FLOAT_MAX1440/GEN_FLOAT_LIMIT1200), not retyped
// from the spec by hand, so an override clamps to exactly the same bounds
// an ordinary (non-overridden) preset already does. genMetas's own
// GEN_INT_LIMITFC case keeps its own copy of initialFilterFc's 1500/13500
// (kFilterFcMin/kFilterFcMax in SoundFont.cpp) rather than reading this
// table - that clamp covers all ~59 SF2 generators, not just the ones a
// song can override, so it stays independent of this override-specific
// table; a comment there cross-references this one so the two numbers
// can't silently drift without someone noticing.
//
// Shared, via the lookup functions below, between the XML load path
// (name -> id, Song.cpp) and the save path (id -> name) so both directions
// stay in sync by construction rather than as two independently-maintained
// switch statements that could drift; and between every SoundFontVoice
// effectiveXxx() substitution and this same range data, so a generator's
// valid range is defined in exactly one place.

// The SF2 spec's own generator-id numbering, scoped to just the ids this
// table covers (see the doc comment above for why that's a small subset,
// not the full ~59). A real C++ enum, not raw ints, so a call site names a
// generator (SF2Generator::DecayVolEnv) instead of trusting a same-line
// comment (`// 36 = decayVolEnv`) the way SoundFont.cpp's unrelated,
// full-spec genMetas table still does - genMetas indexes positionally over
// every generator id at parse time and has no reason to share this type.
// Fixed underlying `int` (same width the map/table/XML round-trip already
// used before this enum existed) so an out-of-range id - e.g.
// GeneratorOverrideTests.cpp's "unrecognized id" case - can still be
// produced via a plain static_cast without invoking undefined behavior.
enum class SF2Generator : int {
  InitialFilterFc    = 8,

  // Volume envelope.
  DelayVolEnv         = 33,
  AttackVolEnv        = 34,
  HoldVolEnv          = 35,
  DecayVolEnv         = 36,
  SustainVolEnv       = 37,
  ReleaseVolEnv       = 38,
  KeynumToVolEnvHold  = 39,
  KeynumToVolEnvDecay = 40,
};

struct SF2GeneratorEntry { const char * name; SF2Generator id; float min; float max; };

inline constexpr SF2GeneratorEntry kSF2GeneratorTable[] = {
  {"initialFilterFc",     SF2Generator::InitialFilterFc,    1500.0f, 13500.0f},

  // Volume envelope - all timecents except sustainVolEnv
  // (centibels of attenuation, 0 = full level, 1440 = silence) and the two
  // keynumTo* generators (timecents *per key*, applied relative to key 60 -
  // see EnvelopeState.h's own keynum-tracking math).
  {"delayVolEnv",         SF2Generator::DelayVolEnv,         -12000.0f, 5000.0f},
  {"attackVolEnv",        SF2Generator::AttackVolEnv,        -12000.0f, 8000.0f},
  {"holdVolEnv",          SF2Generator::HoldVolEnv,          -12000.0f, 5000.0f},
  {"decayVolEnv",         SF2Generator::DecayVolEnv,         -12000.0f, 8000.0f},
  {"sustainVolEnv",       SF2Generator::SustainVolEnv,       0.0f, 1440.0f},
  {"releaseVolEnv",       SF2Generator::ReleaseVolEnv,       -12000.0f, 8000.0f},
  {"keynumToVolEnvHold",  SF2Generator::KeynumToVolEnvHold,  -1200.0f, 1200.0f},
  {"keynumToVolEnvDecay", SF2Generator::KeynumToVolEnvDecay, -1200.0f, 1200.0f},
};

inline std::optional<SF2Generator> sf2GeneratorIdForName(std::string_view name) {
  for (auto & entry : kSF2GeneratorTable) {
    if (name == entry.name) return entry.id;
  }
  return std::nullopt;
}

inline const char * sf2GeneratorNameForId(SF2Generator id) {
  for (auto & entry : kSF2GeneratorTable) {
    if (entry.id == id) return entry.name;
  }
  return nullptr;
}

inline const SF2GeneratorEntry * sf2GeneratorEntryForId(SF2Generator id) {
  for (auto & entry : kSF2GeneratorTable) {
    if (entry.id == id) return &entry;
  }
  return nullptr;
}

// Clamps `value` to generator `id`'s own valid range - a no-op (returns
// `value` unclamped) if `id` isn't in the table at all, which shouldn't
// happen for anything actually reaching this from generator_overrides_
// (every entry there came from a successful sf2GeneratorIdForName() lookup
// in the first place), but isn't a reason to crash if it somehow did.
inline float clampToGeneratorRange(SF2Generator id, float value) {
  auto entry = sf2GeneratorEntryForId(id);
  return entry ? std::clamp(value, entry->min, entry->max) : value;
}

#endif
