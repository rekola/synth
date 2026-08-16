#include "TestFramework.h"

#include "../src/SF2Modulator.h"
#include "../src/SoundFont.h"
#include "../src/ChannelConfiguration.h"
#include "../src/SphericalPosition.h"
#include "../src/SendLevels.h"
#include "../src/TrackState.h"
#include "../src/InstrumentTrackState.h"
#include "../src/RenderContext.h"
#include "../src/Track.h"
#include "../src/AudioBuffer.h"
#include "../src/NoteCoordinate.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace SF2Mod;

#ifndef TESTS_SCRATCH_DIR
#define TESTS_SCRATCH_DIR "."
#endif

TEST(parse_mod_source_decodes_index_and_cc_flag) {
  // Plain General Controller, no flags: ChannelPressure = 13.
  auto s = parseModSource(13);
  CHECK(!s.isMidiCC);
  CHECK(s.index == 13);
  CHECK(!s.decreasing);
  CHECK(!s.bipolar);
  CHECK(s.curve == CurveType::Linear);

  // MIDI CC flag (bit 7) set: CC#1 (mod wheel).
  auto cc = parseModSource(0x80 | 1);
  CHECK(cc.isMidiCC);
  CHECK(cc.index == 1);
}

TEST(parse_mod_source_decodes_direction_polarity_and_curve) {
  auto decreasing = parseModSource(0x100 | 13);
  CHECK(decreasing.index == 13);
  CHECK(decreasing.decreasing);
  CHECK(!decreasing.bipolar);

  auto bipolar = parseModSource(0x200 | 13);
  CHECK(!bipolar.decreasing);
  CHECK(bipolar.bipolar);

  auto convex = parseModSource(static_cast<uint16_t>(2 << 10) | 13);
  CHECK(convex.curve == CurveType::Convex);

  auto combined = parseModSource(static_cast<uint16_t>((3 << 10) | 0x380 | 13));
  CHECK(combined.index == 13);
  CHECK(combined.decreasing);
  CHECK(combined.bipolar);
  CHECK(combined.curve == CurveType::Switch);
}

TEST(apply_source_curve_linear_unipolar) {
  Source s;
  s.curve = CurveType::Linear;
  CHECK_NEAR(applySourceCurve(0.0f, s), 0.0f, 1e-6f);
  CHECK_NEAR(applySourceCurve(0.5f, s), 0.5f, 1e-6f);
  CHECK_NEAR(applySourceCurve(1.0f, s), 1.0f, 1e-6f);
}

TEST(apply_source_curve_clamps_out_of_range_input) {
  Source s;
  s.curve = CurveType::Linear;
  CHECK_NEAR(applySourceCurve(-0.5f, s), 0.0f, 1e-6f);
  CHECK_NEAR(applySourceCurve(1.5f, s), 1.0f, 1e-6f);
}

TEST(apply_source_curve_decreasing_inverts_input) {
  Source s;
  s.curve = CurveType::Linear;
  s.decreasing = true;
  CHECK_NEAR(applySourceCurve(0.0f, s), 1.0f, 1e-6f);
  CHECK_NEAR(applySourceCurve(1.0f, s), 0.0f, 1e-6f);
}

TEST(apply_source_curve_bipolar_remaps_to_minus_one_to_one) {
  Source s;
  s.curve = CurveType::Linear;
  s.bipolar = true;
  CHECK_NEAR(applySourceCurve(0.0f, s), -1.0f, 1e-6f);
  CHECK_NEAR(applySourceCurve(0.5f, s), 0.0f, 1e-6f);
  CHECK_NEAR(applySourceCurve(1.0f, s), 1.0f, 1e-6f);
}

TEST(apply_source_curve_switch_thresholds_at_midpoint) {
  Source s;
  s.curve = CurveType::Switch;
  CHECK_NEAR(applySourceCurve(0.4f, s), 0.0f, 1e-6f);
  CHECK_NEAR(applySourceCurve(0.6f, s), 1.0f, 1e-6f);
}

TEST(apply_source_curve_concave_and_convex_meet_boundary_conditions_and_are_mirrored) {
  Source concave;
  concave.curve = CurveType::Concave;
  Source convex;
  convex.curve = CurveType::Convex;

  CHECK_NEAR(applySourceCurve(0.0f, concave), 0.0f, 1e-5f);
  CHECK_NEAR(applySourceCurve(1.0f, concave), 1.0f, 1e-5f);
  CHECK_NEAR(applySourceCurve(0.0f, convex), 0.0f, 1e-5f);
  CHECK_NEAR(applySourceCurve(1.0f, convex), 1.0f, 1e-5f);

  // Concave (audio-taper sense): slow near 0, fast near 1 - below the
  // diagonal at the midpoint. Convex is the mirror image, above it.
  CHECK(applySourceCurve(0.5f, concave) < 0.5f);
  CHECK(applySourceCurve(0.5f, convex) > 0.5f);
}

TEST(same_identity_compares_src_dest_amtsrc_trans_only) {
  Connection a{ 13, 6, 0, 0, 10 };
  Connection b{ 13, 6, 0, 0, 99 }; // different amount, same identity
  Connection c{ 13, 8, 0, 0, 10 }; // different dest
  CHECK(sameIdentity(a, b));
  CHECK(!sameIdentity(a, c));
}

TEST(merge_modulators_concatenates_when_no_identity_overlaps) {
  std::vector<Connection> base{ Connection{ 13, 6, 0, 0, 10 } };
  std::vector<Connection> overrides{ Connection{ 13, 8, 0, 0, 20 } };
  auto result = mergeModulators(base, overrides);
  CHECK(result.size() == 2);
}

TEST(merge_modulators_override_replaces_matching_identity) {
  std::vector<Connection> base{ Connection{ 13, 6, 0, 0, 10 } };
  std::vector<Connection> overrides{ Connection{ 13, 6, 0, 0, 0 } }; // same identity, amount 0 (disabled)
  auto result = mergeModulators(base, overrides);
  CHECK(result.size() == 1);
  CHECK(result[0].amount == 0);
}

TEST(merge_modulators_handles_empty_base_or_overrides) {
  std::vector<Connection> one{ Connection{ 13, 6, 0, 0, 10 } };
  std::vector<Connection> empty;

  auto fromEmptyBase = mergeModulators(empty, one);
  CHECK(fromEmptyBase.size() == 1);
  CHECK(fromEmptyBase[0].amount == 10);

  auto fromEmptyOverrides = mergeModulators(one, empty);
  CHECK(fromEmptyOverrides.size() == 1);
  CHECK(fromEmptyOverrides[0].amount == 10);
}

#ifdef SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS
TEST(merge_modulators_heuristic_default_injection_shape) {
  // Models how SoundFont.cpp's tsf_load_presets() injects the GM-family
  // heuristic default: mergeModulators(existing, { heuristic }) only when
  // isChannelPressureSourced() found nothing already in `existing` (that
  // well-formedness check itself is caller-side logic, private to
  // SoundFont.cpp - covered by the end-to-end fixture test, not here).
  // This test only confirms mergeModulators' own behavior in that shape:
  // a heuristic default merged onto an empty/unrelated base is added
  // as-is, unmodified. Doesn't itself reference any ifdef'd symbol (pure
  // mergeModulators() behavior with heuristic-shaped test data), but
  // guarded anyway so every heuristic-themed test lives and dies
  // together with the feature it exercises.
  Connection heuristicDefault{ static_cast<uint16_t>(GeneralController::ChannelPressure), 6,
                                static_cast<uint16_t>(GeneralController::NoController), 0, 10 };
  std::vector<Connection> unrelatedExisting{ Connection{ 0, 17, 0, 0, 500 } }; // some other, unrelated identity
  auto result = mergeModulators(unrelatedExisting, { heuristicDefault });
  CHECK(result.size() == 2);
  CHECK(result[1].amount == 10);
}
#endif // SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS

TEST(is_channel_pressure_sourced_requires_exact_source_and_no_secondary_scaling) {
  auto channelPressure = static_cast<uint16_t>(GeneralController::ChannelPressure);
  auto polyPressure = static_cast<uint16_t>(GeneralController::PolyPressure);
  auto noController = static_cast<uint16_t>(GeneralController::NoController);
  auto noteOnVelocity = static_cast<uint16_t>(GeneralController::NoteOnVelocity);

  CHECK(isChannelPressureSourced(Connection{ channelPressure, 6, noController, 0, 10 }));
  CHECK(isChannelPressureSourced(Connection{ channelPressure, 8, noController, 0, 2400 })); // any destination

  CHECK(!isChannelPressureSourced(Connection{ polyPressure, 6, noController, 0, 10 })); // wrong source
  CHECK(!isChannelPressureSourced(Connection{ channelPressure, 6, noteOnVelocity, 0, 10 })); // secondary scaling

  // MIDI CC #13, not the General Controller ChannelPressure (same raw
  // index, but the CC flag changes its meaning entirely).
  CHECK(!isChannelPressureSourced(Connection{ static_cast<uint16_t>(0x80 | 13), 6, noController, 0, 10 }));
}

TEST(evaluate_channel_pressure_modulator_scales_amount_by_curve) {
  Connection c{ static_cast<uint16_t>(GeneralController::ChannelPressure), 6,
                static_cast<uint16_t>(GeneralController::NoController), 0, 10 };
  CHECK_NEAR(evaluateChannelPressureModulator(c, 0.5f), 5.0f, 1e-5f);
  CHECK_NEAR(evaluateChannelPressureModulator(c, 0.0f), 0.0f, 1e-5f);
  CHECK_NEAR(evaluateChannelPressureModulator(c, 1.0f), 10.0f, 1e-5f);
}

TEST(evaluate_channel_pressure_modulator_applies_absolute_value_transform) {
  // Bipolar source (bit 9 set): at pressure01 = 0, applySourceCurve gives
  // -1, so a positive amount would normally yield a negative contribution -
  // trans = 2 (Absolute Value) should flip that back to positive.
  auto bipolarSrc = static_cast<uint16_t>(static_cast<uint16_t>(GeneralController::ChannelPressure) | 0x200);
  Connection c{ bipolarSrc, 6, static_cast<uint16_t>(GeneralController::NoController), 2, 10 };
  CHECK_NEAR(evaluateChannelPressureModulator(c, 0.0f), 10.0f, 1e-5f);
}

// ---------------------------------------------------------------------
// End-to-end: a minimal synthetic .sf2 built at test time (no checked-in
// binary fixture - tests/fixtures/ is otherwise 100% text/XML), covering
// real, file-authored channel-pressure modulators (parsing/merging in
// SoundFont.cpp's tsf_load_presets(), evaluation in SoundFontVoice::
// render()), SF2 voice lifecycle, and (only when
// SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS is defined) the GM-family
// heuristic default table's injection/gate logic. Exercises the real
// public SoundFont/Instrument/TrackState API, not any internal seam -
// the private tsf_hydra_*/tsf_region types have no test-only exposure,
// by design.
// ---------------------------------------------------------------------

namespace {

  using Bytes = std::vector<uint8_t>;

  void appendU8(Bytes & b, uint8_t v) { b.push_back(v); }
  void appendU16(Bytes & b, uint16_t v) { appendU8(b, static_cast<uint8_t>(v & 0xFF)); appendU8(b, static_cast<uint8_t>((v >> 8) & 0xFF)); }
  void appendI16(Bytes & b, int16_t v) { appendU16(b, static_cast<uint16_t>(v)); }
  void appendU32(Bytes & b, uint32_t v) { appendU16(b, static_cast<uint16_t>(v & 0xFFFF)); appendU16(b, static_cast<uint16_t>((v >> 16) & 0xFFFF)); }
  void appendFourCC(Bytes & b, const char * cc) { for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(cc[i])); }
  void appendName20(Bytes & b, const std::string & name) {
    for (size_t i = 0; i < 20; i++) b.push_back(i < name.size() ? static_cast<uint8_t>(name[i]) : 0);
  }

  // Wraps `payload` as a plain (non-LIST/RIFF) chunk: fourcc + size + payload.
  void appendChunk(Bytes & out, const char * fourcc, const Bytes & payload) {
    appendFourCC(out, fourcc);
    appendU32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
  }

  // Wraps `payload` as a LIST chunk of the given 4-byte type ("pdta"/"sdta").
  void appendListChunk(Bytes & out, const char * listType, const Bytes & payload) {
    appendFourCC(out, "LIST");
    appendU32(out, static_cast<uint32_t>(4 + payload.size()));
    appendFourCC(out, listType);
    out.insert(out.end(), payload.begin(), payload.end());
  }

  // One instrument-zone generator: a plain (genOper, shortAmount) pair -
  // covers everything these fixtures need (InitialFilterFc/VibLfoToPitch),
  // never a range generator.
  struct GenSpec { uint16_t oper; int16_t amount; };

  // GenKeyRange (gen 43)'s amount is a packed { lo, hi } byte pair, not a
  // plain shortAmount (see SoundFont.cpp's tsf_hydra_genamount union and
  // its GEN_KEYRANGE case) - this packs a GenSpec{43, ...} amount the same
  // way, so exclusive-class tests can give two regions non-overlapping
  // key ranges (e.g. two different GM percussion keys) within one preset.
  int16_t packKeyRange(uint8_t lo, uint8_t hi) {
    return static_cast<int16_t>(static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8));
  }

  // MIDI-key-number -> frequency, the same 12-TET formula Tuner::
  // getFrequency(Tuning::PERCUSSION, ...) uses - lets exclusive-class
  // tests select a specific region by key the same way a real percussion
  // note-on would, via SoundFontInstrument::playNote()'s own
  // frequency->midiKey round trip.
  float frequencyForMidiKey(int key) {
    return 440.0f * std::pow(2.0f, static_cast<float>(key - 69) / 12.0f);
  }

  // One instrument-zone modulator, in the same raw packed-field shape
  // SF2Mod::Connection itself uses.
  struct ModSpec { uint16_t src, dest, amtSrc, trans; int16_t amount; };

  struct PresetSpec {
    std::string name;
    uint16_t program;
    std::vector<GenSpec> gens; // besides the always-appended GenSampleID
    std::vector<ModSpec> mods;
    // Number of identical instrument zones (all covering the full key/vel
    // range, so all of them always match) - real GM patches commonly ship
    // stereo L/R sample pairs or velocity layers, which makes
    // SoundFontInstrument::playNote() return a *group* TrackState wrapping
    // several SoundFontVoice children instead of a single voice directly.
    // Defaults to 1 (a bare voice) - set to 2+ to exercise the group path.
    int region_count = 1;
    // Per-region generator override, keyed by region index - when
    // non-empty, region_gens[region] REPLACES `gens` for that region
    // (rather than being merged/added to it, to avoid ambiguity over
    // whether a repeated generator opcode should overwrite or accumulate).
    // Lets a region_count>1 preset give its regions genuinely different
    // envelopes (e.g. different release times), unlike plain `gens` which
    // applies identically to every region.
    std::vector<std::vector<GenSpec>> region_gens;
    // GM bank number - defaults to 0, appended last so every existing
    // positional PresetSpec{...} initializer above is unaffected. 128 is
    // the GM percussion-bank convention (SoundFontInstrument::
    // getDefaultExtent()/applyPercussionOffset()), needed to exercise the
    // percussion-offset mechanism from a fixture.
    uint16_t bank = 0;
  };

  // Builds a minimal single-sample, N-preset (each with exactly one
  // instrument, one zone, no key/vel range restriction) .sf2 file and
  // writes it to `path`. Every preset shares the same sample data (a long
  // sine wave, no looping needed for the handful of frames these tests
  // render unless a preset opts into looping via region_gens) - only the
  // generators/modulators/loop points listed in `presets` differ.
  void writeMinimalSf2(const std::string & path, const std::vector<PresetSpec> & presets) {
    // Shared sample: long enough that no test render runs off the end
    // even with a slightly modulated pitch ratio.
    const uint32_t kSampleCount = 200000;
    std::vector<int16_t> pcm(kSampleCount);
    for (uint32_t i = 0; i < kSampleCount; i++) {
      pcm[i] = static_cast<int16_t>(16000.0 * std::sin(2.0 * M_PI * static_cast<double>(i) / 97.0));
    }
    Bytes smpl_bytes;
    for (auto s : pcm) appendI16(smpl_bytes, s);

    Bytes shdr_bytes;
    appendName20(shdr_bytes, "sine");
    appendU32(shdr_bytes, 0);            // start
    appendU32(shdr_bytes, kSampleCount); // end
    // Real loop points, well inside the sample - harmless to every existing
    // test (none of them set GEN_LOOPMODE, so loop_mode stays NONE and
    // these are simply never read), but needed for the looping-voice
    // regression test below, which does opt a preset into looping.
    appendU32(shdr_bytes, 1000);         // startLoop
    appendU32(shdr_bytes, 9000);         // endLoop
    appendU32(shdr_bytes, 44100);        // sampleRate
    appendU8(shdr_bytes, 60);            // originalPitch (C4)
    appendU8(shdr_bytes, 0);             // pitchCorrection
    appendU16(shdr_bytes, 0);            // sampleLink
    appendU16(shdr_bytes, 1);            // sampleType (monoSample)
    // Terminal "EOS" shdr record - name conventionally "EOS", fields unused.
    appendName20(shdr_bytes, "EOS");
    appendU32(shdr_bytes, 0); appendU32(shdr_bytes, 0); appendU32(shdr_bytes, 0); appendU32(shdr_bytes, 0); appendU32(shdr_bytes, 0);
    appendU8(shdr_bytes, 0); appendU8(shdr_bytes, 0); appendU16(shdr_bytes, 0); appendU16(shdr_bytes, 0);

    Bytes phdr_bytes, pbag_bytes, pgen_bytes;
    Bytes inst_bytes, ibag_bytes, igen_bytes, imod_bytes;

    uint16_t pbag_index = 0, pgen_index = 0;
    uint16_t ibag_index = 0, igen_index = 0, imod_index = 0;

    for (size_t i = 0; i < presets.size(); i++) {
      const auto & p = presets[i];

      // phdr: one zone (this preset's own instrument).
      appendName20(phdr_bytes, p.name);
      appendU16(phdr_bytes, p.program);
      appendU16(phdr_bytes, p.bank);
      appendU16(phdr_bytes, pbag_index);
      appendU32(phdr_bytes, 0); appendU32(phdr_bytes, 0); appendU32(phdr_bytes, 0); // library/genre/morphology

      // pbag: one generator (GenInstrument -> this preset's own instrument
      // index i), no preset-level modulators.
      appendU16(pbag_bytes, pgen_index);
      appendU16(pbag_bytes, 0); // modNdx (pmod array is empty for every zone)
      pbag_index++;

      appendU16(pgen_bytes, 41); // GenInstrument
      appendU16(pgen_bytes, static_cast<uint16_t>(i)); // genAmount.wordAmount = instrument index
      pgen_index++;

      // inst: one or more identical zones (see PresetSpec::region_count).
      appendName20(inst_bytes, p.name);
      appendU16(inst_bytes, ibag_index);

      for (int region = 0; region < p.region_count; region++) {
        // ibag: this zone's own generators/modulators ranges.
        appendU16(ibag_bytes, igen_index);
        appendU16(ibag_bytes, imod_index);
        ibag_index++;

        auto & region_generators = (static_cast<size_t>(region) < p.region_gens.size()) ? p.region_gens[static_cast<size_t>(region)] : p.gens;
        for (auto & g : region_generators) {
          appendU16(igen_bytes, g.oper);
          appendI16(igen_bytes, g.amount);
          igen_index++;
        }
        // GenSampleID must be last in the zone's generator list (SF2
        // convention this codebase's own parser relies on) and always
        // references the one shared sample (index 0).
        appendU16(igen_bytes, 53); // GenSampleID
        appendU16(igen_bytes, 0);  // sample index
        igen_index++;

        // Only the first zone's modulators matter for these fixtures
        // (region_count > 1 is only ever used without any mods).
        if (region == 0) {
          for (auto & m : p.mods) {
            appendU16(imod_bytes, m.src);
            appendU16(imod_bytes, m.dest);
            appendI16(imod_bytes, m.amount);
            appendU16(imod_bytes, m.amtSrc);
            appendU16(imod_bytes, m.trans);
            imod_index++;
          }
        }
      }
    }

    // Terminal ("EOP"/"EOI") sentinel records - their *Ndx fields mark the
    // end of the last real zone's generator/modulator ranges; their own
    // content is otherwise unused.
    appendName20(phdr_bytes, "EOP");
    appendU16(phdr_bytes, 0); appendU16(phdr_bytes, 0); appendU16(phdr_bytes, pbag_index);
    appendU32(phdr_bytes, 0); appendU32(phdr_bytes, 0); appendU32(phdr_bytes, 0);
    appendU16(pbag_bytes, pgen_index); appendU16(pbag_bytes, 0);

    appendName20(inst_bytes, "EOI");
    appendU16(inst_bytes, ibag_index);
    appendU16(ibag_bytes, igen_index); appendU16(ibag_bytes, imod_index);

    Bytes pdta_payload;
    appendChunk(pdta_payload, "phdr", phdr_bytes);
    appendChunk(pdta_payload, "pbag", pbag_bytes);
    appendChunk(pdta_payload, "pmod", Bytes{}); // no preset-level modulators anywhere in these fixtures
    appendChunk(pdta_payload, "pgen", pgen_bytes);
    appendChunk(pdta_payload, "inst", inst_bytes);
    appendChunk(pdta_payload, "ibag", ibag_bytes);
    appendChunk(pdta_payload, "imod", imod_bytes);
    appendChunk(pdta_payload, "igen", igen_bytes);
    appendChunk(pdta_payload, "shdr", shdr_bytes);

    Bytes sdta_payload;
    appendChunk(sdta_payload, "smpl", smpl_bytes);

    Bytes body;
    appendListChunk(body, "sdta", sdta_payload);
    appendListChunk(body, "pdta", pdta_payload);

    Bytes file;
    appendFourCC(file, "RIFF");
    appendU32(file, static_cast<uint32_t>(4 + body.size()));
    appendFourCC(file, "sfbk");
    file.insert(file.end(), body.begin(), body.end());

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(file.data()), static_cast<std::streamsize>(file.size()));
  }

  // Sum of |low - high| over the Main channel - 0 (up to float noise) when
  // channel pressure had no effect, clearly nonzero when it did.
  float mainChannelDifference(const AudioBuffer & low, const AudioBuffer & high) {
    auto n = low.numberOfFrames();
    if (n != high.numberOfFrames() || !low.hasChannel(Channel::Main) || !high.hasChannel(Channel::Main)) return -1.0f;
    auto a = low.getChannelData(0), b = high.getChannelData(0);
    float total = 0.0f;
    for (int i = 0; i < n; i++) total += std::fabs(a[i] - b[i]);
    return total;
  }

}

#ifdef SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS
TEST(sf2_channel_pressure_heuristic_end_to_end) {
  std::vector<PresetSpec> presets = {
    { "Piano",       0,  {},                                       {} },                                        // 0: disabled family
    { "DrawbarOrgan", 16, {},                                      {} },                                        // 1: vibrato, gate-exempt (16-20)
    { "AccordionA",  21, {},                                       {} },                                        // 2: vibrato family, vibLfoToPitch=0 - gate fails
    { "AccordionB",  21, { GenSpec{ 6, 50 } },                      {} },                                        // 3: vibLfoToPitch=50 (VibLfoToPitch) - gate passes
    { "PadA",        89, { GenSpec{ 8, 8000 } },                    {} },                                        // 4: InitialFilterFc=8000 - gate passes
    { "PadB",        89, {},                                       {} },                                        // 5: InitialFilterFc left at spec default (13500) - gate fails
    { "PadC",        89, { GenSpec{ 8, 8000 } },
      { ModSpec{ static_cast<uint16_t>(GeneralController::ChannelPressure), 8, static_cast<uint16_t>(GeneralController::NoController), 0, 0 } } }, // 6: file already manages channel pressure (explicit amount-0 override) - heuristic must not add its own
  };

  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "sf2_channel_pressure_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);

  auto renderAtPressure = [&](size_t preset_index, float pressure) {
    auto instrument = sf.createInstrument(preset_index);
    auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
    voice->applyChannelPressure(pressure);
    return voice->render(8192);
  };

  auto checkUnaffected = [&](size_t preset_index, const char * label) {
    auto low = renderAtPressure(preset_index, 0.0f);
    auto high = renderAtPressure(preset_index, 1.0f);
    auto diff = mainChannelDifference(low, high);
    CHECK(diff >= 0.0f);
    CHECK(diff < 1e-4f);
    (void)label;
  };

  auto checkAffected = [&](size_t preset_index, const char * label) {
    auto low = renderAtPressure(preset_index, 0.0f);
    auto high = renderAtPressure(preset_index, 1.0f);
    auto diff = mainChannelDifference(low, high);
    CHECK(diff >= 0.0f);
    CHECK(diff > 1e-3f);
    (void)label;
  };

  checkUnaffected(0, "Piano: disabled family, must not change");
  checkAffected(1, "DrawbarOrgan: gate-exempt vibrato, must change");
  checkUnaffected(2, "Accordion with vibLfoToPitch=0: gated, must not change");
  checkAffected(3, "Accordion with vibLfoToPitch!=0: gate passes, must change");
  checkAffected(4, "Pad with real filter: gate passes, must change");
  checkUnaffected(5, "Pad at spec-default filter: gated, must not change");
  checkUnaffected(6, "Pad with explicit amount-0 override: heuristic must not add its own");
}
#endif // SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS

TEST(sf2_channel_pressure_reaches_every_region_in_a_multi_region_group) {
  // Real GM patches commonly ship more than one matching region per note
  // (stereo L/R sample pairs, velocity layers) - SoundFontInstrument::
  // playNote() then returns a group VoiceState wrapping several
  // SoundFontVoice children instead of a single voice directly.
  // VoiceState::applyChannelPressure()'s default must recurse into
  // children (like applyAftertouch already does) for the pressure to
  // ever reach those children - a regression test for exactly that,
  // using a 2-region Pad preset (both regions covering the full key/vel
  // range, so both always match) with its own explicitly authored
  // channel-pressure -> filter-cutoff modulator (not relying on the
  // GM-family default heuristic, which is off by default - see
  // SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS - and, even when enabled, only
  // a file's own real modulators should be needed for this particular
  // regression to hold).
  std::vector<PresetSpec> presets = {
    { "StereoPad", 89, { GenSpec{ 8, 8000 } },
      { ModSpec{ static_cast<uint16_t>(GeneralController::ChannelPressure), 8, static_cast<uint16_t>(GeneralController::NoController), 0, 2400 } },
      /*region_count=*/2 },
  };

  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "sf2_channel_pressure_group_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);

  auto instrument = sf.createInstrument(0);

  auto voice_low = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  voice_low->applyChannelPressure(0.0f);
  auto low = voice_low->render(8192);

  auto voice_high = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  voice_high->applyChannelPressure(1.0f);
  auto high = voice_high->render(8192);

  CHECK(mainChannelDifference(low, high) > 1e-3f);
}

TEST(sf2_looping_voice_becomes_inactive_after_stop_note) {
  // Real GM patches are almost always looping (sustained instruments need
  // to keep sounding for as long as a key is held, well past the end of
  // one physical sample recording) - unlike this fixture's other presets,
  // which never set GEN_LOOPMODE (54) and so never loop at all. For a
  // looping region, sourceSamplePosition_ never naturally reaches
  // voiceRegion_->end (the loop wraps it back - see SoundFontVoice::
  // render()'s loopStart_/loopEnd_ handling), so SoundFontVoice::
  // isActive() depends entirely on the amp envelope reaching DONE, which
  // in turn depends on stopNote() actually driving it through RELEASE.
  // This is a regression test for exactly that path staying live (not a
  // reproduction of a confirmed bug - EnvelopeState's RELEASE->DONE
  // transition reads correct by inspection): a leaked, permanently-active
  // looping voice would fail this test by never going inactive.
  std::vector<PresetSpec> presets = {
    { "LoopingPad", 89, { GenSpec{ 54, 1 } /* SampleModes = loop continuously */ }, {} },
  };

  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "sf2_looping_voice_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);

  auto instrument = sf.createInstrument(0);
  auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  CHECK(voice->isActive());

  // Render a bit while held, to confirm the loop is actually being
  // exercised (voice stays active well past the underlying sample's own
  // short loop region).
  for (int i = 0; i < 4; i++) {
    voice->render(4096);
    CHECK(voice->isActive());
  }

  voice->stopNote();

  // Default envelope has no configured release time, so SoundFontVoice::
  // stopNote() falls back to TSF_FASTRELEASETIME (10ms, ~441 samples at
  // 44100Hz) - render comfortably past that and expect isActive() to have
  // dropped to false, even though the underlying sample is still looping
  // and would otherwise play forever.
  bool became_inactive = false;
  for (int i = 0; i < 20 && !became_inactive; i++) {
    voice->render(4096);
    if (!voice->isActive()) became_inactive = true;
  }

  CHECK(became_inactive);
}

TEST(sf2_voice_with_modulator_child_fully_reclaims_on_stop) {
  // GenericInstrument::playNote() (GenericInstrument.h) attaches a
  // song-configured modulator child (see e.g. songs/subtractive_test.xml's
  // <genericInstrument name="Cello"><oscillator .../></genericInstrument>)
  // directly onto whatever TrackState the wrapped instrument's own
  // playNote() returns - for a single-region SF2 patch that's the bare
  // SoundFontVoice itself (SoundFontInstrument::playNote()'s
  // voices.size()==1 case). Mirrors that exact wiring without needing the
  // full GenericInstrument/InstrumentProvider machinery. Regression test
  // for a confirmed bug: SoundFontVoice::stopNote()/killNote() used to be
  // documented "do not stop children", leaving a modulator child's own
  // envelope permanently short of DONE (it's also never actually rendered
  // by SoundFontVoice::render(), which doesn't touch children_ at all -
  // nothing today reads a modulator's audio output - so process() never
  // even ran on it to advance a graceful RELEASE either way). Fixed by
  // having both killNote() and stopNote() call VoiceState::killNote() on
  // children - immediate, not a release these unrendered children could
  // never actually complete on their own.
  std::vector<PresetSpec> presets = {
    { "LeadVoice", 80, {}, {} },
    { "Modulator", 80, {}, {} },
  };

  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "sf2_modulator_child_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);

  auto lead_instrument = sf.createInstrument(0);
  auto modulator_instrument = sf.createInstrument(1);

  auto voice = lead_instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  auto modulator = modulator_instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 1.0f, 60, SendLevels{});
  voice->addChild(12345, std::move(modulator));

  CHECK(voice->isActive());

  voice->stopNote();

  bool became_fully_inactive = false;
  for (int i = 0; i < 20 && !became_fully_inactive; i++) {
    voice->render(4096);
    // Base TrackState::isActive() (used by getVoiceCount()'s recursion,
    // and by anything that treats this as a generic voice tree) ORs in
    // the modulator child - if the modulator never got stopNote()'d, this
    // never goes false even though the audible lead voice itself finished.
    if (voice->getVoiceCount() == 0) became_fully_inactive = true;
  }

  CHECK(became_fully_inactive);
}

TEST(sf2_instrument_track_state_reclaims_looping_multi_region_voice) {
  // Both tests above drive SoundFontVoice/the group VoiceState directly via
  // the bare Instrument::playNote()/stopNote() API, bypassing
  // InstrumentTrackState's own voices_/clearFinishedVoices() machinery
  // entirely - the actual production path (Player.cpp's note-on/off ->
  // RenderContext pending events -> InstrumentTrackState::render(frames,
  // instruments, context), see the top of that method) never exercised by
  // either. This test drives that real path instead, to check whether a
  // leak specific to the InstrumentTrackState wiring (not the leaf voice
  // math already confirmed correct above) reproduces here.
  std::vector<PresetSpec> presets = {
    { "LoopingStereoPad", 89, { GenSpec{ 54, 1 } }, {}, /*region_count=*/2 },
  };

  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "sf2_instrument_track_state_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);

  std::vector<std::unique_ptr<Track> > instruments;
  instruments.push_back(sf.createInstrument(0));

  InstrumentTrackState state(config, /*solo=*/false, /*muted=*/false, /*track_id=*/0, /*instrument_id=*/0,
                              SphericalPosition{}, SendLevels{});
  RenderContext context(config);
  context.setBpm(120.0f);

  // Note-on: column/id 0, real frequency+velocity.
  context.addPendingEvent(0, 0, 0, 440.0f, 0.8f, 60);
  state.render(4096, instruments, context);
  CHECK(state.isActive());

  for (int i = 0; i < 4; i++) {
    state.render(4096, instruments, context);
    CHECK(state.isActive());
  }

  // Note-off: velocity 0.0f => TrackEvent::isOff() => stopVoices(0).
  context.addPendingEvent(0, 0, 0, 0.0f, 0.0f, -1);

  bool became_inactive = false;
  for (int i = 0; i < 20 && !became_inactive; i++) {
    state.render(4096, instruments, context);
    if (!state.isActive()) became_inactive = true;
  }

  CHECK(became_inactive);
}

TEST(sf2_second_stop_note_does_not_resurrect_an_already_done_sibling_region) {
  // InstrumentTrackState::stopVoices() calls voice->stopNote() every time
  // a column's note-off fires *and* every time a new note-on retriggers a
  // column still holding an old, not-yet-reaped release-tail voice (see
  // stopVoices()'s own call site right before addVoice() in both
  // InstrumentTrackState.h and Player.cpp's PLAY_NOTE handler) - both call
  // stopNote() only when the top-level voice's own isActive() is still
  // true. For a multi-region group (VoiceState::isActive() ORs over
  // children - real GM patches with stereo/velocity-layered regions
  // commonly have per-region envelopes that finish releasing at different
  // times), that guard only proves *some* child is still active, not that
  // *every* child is - the base VoiceState::stopNote() recurses into every
  // child unconditionally regardless of each child's own state. A region
  // whose envelope already reached DONE (isActive()==false) then gets a
  // second stopNote(), which - before the fix this is a regression test
  // for - unconditionally forced ampenv_ from DONE back into RELEASE
  // (EnvelopeState::nextSegment(SUSTAIN) doesn't check the current
  // segment), resurrecting a voice that had already finished. Under
  // ordinary repeated note-on retriggering on the same track/column (the
  // overwhelmingly common case, not an edge case) this can keep a
  // multi-region SF2 group perpetually "active" even though its audible
  // regions individually finished long ago - the actual, broad "SoundFont
  // voices never really stop" mechanism, not specific to any song feature
  // like FM-modulator children.
  std::vector<PresetSpec> presets = {
    {
      "StereoPad", 89, /*gens=*/{}, /*mods=*/{}, /*region_count=*/2,
      /*region_gens=*/{
        { GenSpec{ 38, -12000 } }, // region 0: ReleaseVolEnv clamped to ~0s (TSF_FASTRELEASETIME, ~441 samples)
        { GenSpec{ 38, 0 } },      // region 1: ReleaseVolEnv = 0 timecents = 1.0s (~44100 samples) - deliberately much longer
      },
    },
  };

  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "sf2_resurrection_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);

  auto instrument = sf.createInstrument(0);
  auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  CHECK(voice->isActive());
  CHECK(voice->getChildren().size() == 2);

  // First stop (real note-off): both regions start releasing.
  voice->stopNote();

  // Render past region 0's short release but well short of region 1's
  // long one, then confirm the group is in exactly the expected mixed
  // state: one child already done, the other still genuinely active.
  voice->render(4096);

  int done_count = 0, active_count = 0;
  for (auto & [ id, child ] : voice->getChildren()) {
    if (child->isActive()) active_count++; else done_count++;
  }
  CHECK(done_count == 1);
  CHECK(active_count == 1);
  CHECK(voice->isActive()); // group ORs over children - still true via region 1

  // Second stop (a retrigger on the same column, per stopVoices()'s own
  // "if (voice->isActive()) voice->stopNote();" guard - satisfied here
  // since the group is still active via region 1).
  voice->stopNote();

  // The already-finished region must still be done - not resurrected by a
  // stopNote() call that was only ever "authorized" by its sibling.
  int done_after = 0;
  for (auto & [ id, child ] : voice->getChildren()) {
    if (!child->isActive()) done_after++;
  }
  CHECK(done_after == 1);
}

TEST(sf2_looping_multi_region_group_becomes_inactive_after_stop_note) {
  // Real GM patches commonly ship more than one matching region per note
  // (stereo L/R pairs, velocity layers - see PresetSpec::region_count and
  // sf2_channel_pressure_reaches_every_region_in_a_multi_region_group
  // above), which makes SoundFontInstrument::playNote() return a plain,
  // non-overriding VoiceState group wrapping several SoundFontVoice
  // children instead of a single voice directly - unlike
  // sf2_looping_voice_becomes_inactive_after_stop_note above, which only
  // ever exercised a single bare SoundFontVoice. Group VoiceState::
  // isActive() ORs over children and stopVoices()/VoiceState::stopNote()
  // recurses into every child, so this *should* behave identically - this
  // test is here to confirm that holds for the actual group wrapper too,
  // not just a lone voice.
  std::vector<PresetSpec> presets = {
    { "LoopingStereoPad", 89, { GenSpec{ 54, 1 } }, {}, /*region_count=*/2 },
  };

  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "sf2_looping_group_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);

  auto instrument = sf.createInstrument(0);
  auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  CHECK(voice->isActive());

  for (int i = 0; i < 4; i++) {
    voice->render(4096);
    CHECK(voice->isActive());
  }

  voice->stopNote();

  bool became_inactive = false;
  for (int i = 0; i < 20 && !became_inactive; i++) {
    voice->render(4096);
    if (!voice->isActive()) became_inactive = true;
  }

  CHECK(became_inactive);
}

// ---------------------------------------------------------------------
// Identity-based retrigger cutoff (InstrumentTrackState::retriggerVoices())
// and SF2 exclusive-class choking (InstrumentTrackState::
// chokeExclusiveClasses(), VoiceState::getExclusiveClasses()) - see
// plans/sf2-retrigger-cutoff.md. All use a single long (1.0s ReleaseVolEnv,
// GenSpec{38, 0}) authored release, so "did it finish within a handful of
// ~4096-sample blocks" cleanly distinguishes a fast release (~10ms,
// TSF_FASTRELEASETIME) from the voice's own normal release (would still be
// active after the same render time).
// ---------------------------------------------------------------------

TEST(retrigger_voices_fast_releases_same_identity_voice) {
  std::vector<PresetSpec> presets = {
    { "LongRelease", 0, { GenSpec{ 38, 0 } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "retrigger_same_identity_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  InstrumentTrackState state(config, /*solo=*/false, /*muted=*/false, /*track_id=*/0, /*instrument_id=*/0, SphericalPosition{}, SendLevels{});

  state.addVoice(0, instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{}));
  CHECK(state.isActive());

  // Same identity (60) retriggered in the same column - the prior voice
  // must be fast-released, not left on its full 1.0s authored release.
  // Deliberately not adding a replacement voice, so isActive() reflects
  // only the old voice's own fate.
  state.retriggerVoices(0, 60);

  bool became_inactive = false;
  for (int i = 0; i < 8 && !became_inactive; i++) {
    state.renderVoices(4096);
    if (!state.isActive()) became_inactive = true;
  }
  CHECK(became_inactive);
}

TEST(retrigger_voices_does_not_fast_release_a_different_identity) {
  std::vector<PresetSpec> presets = {
    { "LongRelease", 0, { GenSpec{ 38, 0 } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "retrigger_different_identity_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  InstrumentTrackState state(config, /*solo=*/false, /*muted=*/false, /*track_id=*/0, /*instrument_id=*/0, SphericalPosition{}, SendLevels{});

  state.addVoice(0, instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{}));

  // Different identity (61) replacing the same column's note - normal
  // stopNote() (natural release/ring-out), never a fast release.
  state.retriggerVoices(0, 61);

  // Render well past the fast-release window (~10ms) but far short of the
  // voice's full 1.0s authored release - it must still be genuinely
  // active (naturally releasing), not already finished.
  for (int i = 0; i < 4; i++) state.renderVoices(4096); // ~371ms
  CHECK(state.isActive());
}

TEST(retrigger_voices_fast_releases_same_identity_in_a_different_column) {
  std::vector<PresetSpec> presets = {
    { "LongRelease", 0, { GenSpec{ 38, 0 } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "retrigger_cross_column_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  InstrumentTrackState state(config, /*solo=*/false, /*muted=*/false, /*track_id=*/0, /*instrument_id=*/0, SphericalPosition{}, SendLevels{});

  state.addVoice(0, instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{}));

  // Same identity (60), but a note-on for a *different* column (1) - the
  // track-wide scan must still catch and fast-release column 0's voice,
  // since matching is scoped to the whole track, not just the column
  // being written to.
  state.retriggerVoices(1, 60);

  bool became_inactive = false;
  for (int i = 0; i < 8 && !became_inactive; i++) {
    state.renderVoices(4096);
    if (!state.isActive()) became_inactive = true;
  }
  CHECK(became_inactive);
}

TEST(retrigger_voices_does_not_cut_a_31edo_cluster) {
  std::vector<PresetSpec> presets = {
    { "LongRelease", 0, { GenSpec{ 38, 0 } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "retrigger_cluster_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  InstrumentTrackState state(config, /*solo=*/false, /*muted=*/false, /*track_id=*/0, /*instrument_id=*/0, SphericalPosition{}, SendLevels{});

  // A chord: two adjacent-but-distinct 31-EDO step values (60, 61)
  // entered into two columns, neither previously occupied - exact-integer
  // equality means neither retriggerVoices() call finds a match, so
  // neither note ever gets a release call at all (this is a cluster, not
  // a retrigger).
  state.retriggerVoices(0, 60);
  state.addVoice(0, instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{}));
  state.retriggerVoices(1, 61);
  state.addVoice(1, instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 61, SendLevels{}));

  for (int i = 0; i < 4; i++) state.renderVoices(4096);

  std::unordered_map<int, std::vector<ActiveVoiceInfo> > active;
  state.getAllActiveVoices(active);
  auto & voices = active[0];
  CHECK(voices.size() == 2);
  bool has60 = false, has61 = false;
  for (auto & v : voices) {
    if (v.note_value == 60) has60 = true;
    if (v.note_value == 61) has61 = true;
  }
  CHECK(has60);
  CHECK(has61);
}

TEST(choke_exclusive_classes_chokes_a_different_note_value_sharing_class) {
  // Two regions within one preset, non-overlapping key ranges (a real GM
  // percussion kit's open/closed hi-hat shape: different MIDI keys,
  // same exclusiveClass), both long-release so "already inactive" can
  // only mean "fast-released", never "finished naturally".
  std::vector<PresetSpec> presets = {
    { "HiHats", 0, {}, {}, /*region_count=*/2,
      /*region_gens=*/{
        { GenSpec{ 43, packKeyRange(42, 42) }, GenSpec{ 57, 5 }, GenSpec{ 38, 0 } }, // closed hihat, key 42, class 5
        { GenSpec{ 43, packKeyRange(46, 46) }, GenSpec{ 57, 5 }, GenSpec{ 38, 0 } }, // open hihat, key 46, class 5
      } },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "choke_different_note_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  InstrumentTrackState state(config, /*solo=*/false, /*muted=*/false, /*track_id=*/0, /*instrument_id=*/0, SphericalPosition{}, SendLevels{});

  state.addVoice(0, instrument->playNote(config, SphericalPosition{}, frequencyForMidiKey(42), 1.0f, 0.8f, 42, SendLevels{}));
  CHECK(state.isActive());

  // Key 46 (a different note identity - retriggerVoices() alone would
  // never touch key 42's voice) shares exclusiveClass 5 with key 42, so
  // chokeExclusiveClasses() must fast-release it. Deliberately not adding
  // the new voice, so isActive() reflects only the old voice's fate.
  auto voice46 = instrument->playNote(config, SphericalPosition{}, frequencyForMidiKey(46), 1.0f, 0.8f, 46, SendLevels{});
  state.retriggerVoices(1, 46);
  state.chokeExclusiveClasses(*voice46);

  bool became_inactive = false;
  for (int i = 0; i < 8 && !became_inactive; i++) {
    state.renderVoices(4096);
    if (!state.isActive()) became_inactive = true;
  }
  CHECK(became_inactive);
}

TEST(choke_exclusive_classes_does_not_touch_voices_without_a_shared_class) {
  std::vector<PresetSpec> presets = {
    { "HiHatsAndKick", 0, {}, {}, /*region_count=*/3,
      /*region_gens=*/{
        { GenSpec{ 43, packKeyRange(42, 42) }, GenSpec{ 57, 5 }, GenSpec{ 38, 0 } }, // closed hihat, key 42, class 5
        { GenSpec{ 43, packKeyRange(46, 46) }, GenSpec{ 57, 5 }, GenSpec{ 38, 0 } }, // open hihat, key 46, class 5
        { GenSpec{ 43, packKeyRange(36, 36) }, GenSpec{ 38, 0 } },                   // kick, key 36, no class
      } },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "choke_unrelated_class_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  InstrumentTrackState state(config, /*solo=*/false, /*muted=*/false, /*track_id=*/0, /*instrument_id=*/0, SphericalPosition{}, SendLevels{});

  state.addVoice(0, instrument->playNote(config, SphericalPosition{}, frequencyForMidiKey(42), 1.0f, 0.8f, 42, SendLevels{})); // class 5
  state.addVoice(1, instrument->playNote(config, SphericalPosition{}, frequencyForMidiKey(36), 1.0f, 0.8f, 36, SendLevels{})); // no class

  auto voice46 = instrument->playNote(config, SphericalPosition{}, frequencyForMidiKey(46), 1.0f, 0.8f, 46, SendLevels{}); // class 5
  state.retriggerVoices(2, 46);
  state.chokeExclusiveClasses(*voice46);
  state.addVoice(2, move(voice46));

  // Past the fast-release window, short of the 1.0s natural release.
  for (int i = 0; i < 4; i++) state.renderVoices(4096);

  std::unordered_map<int, std::vector<ActiveVoiceInfo> > active;
  state.getAllActiveVoices(active);
  auto & voices = active[0];
  bool has42 = false, has36 = false, has46 = false;
  for (auto & v : voices) {
    if (v.note_value == 42) has42 = true;
    if (v.note_value == 36) has36 = true;
    if (v.note_value == 46) has46 = true;
  }
  CHECK(!has42); // choked - shared class 5 with the new key-46 note
  CHECK(has36);  // untouched - no exclusive class at all
  CHECK(has46);  // the new voice itself
}

TEST(exclusive_class_choke_overrides_normal_release_when_composed_with_retrigger) {
  // Cross-cutting composition case: a voice that both differs in note
  // identity from the incoming note *and* shares an exclusive class with
  // it. retriggerVoices() alone (different identity, same column) would
  // only give it a normal ~1.0s stopNote(); chokeExclusiveClasses() must
  // still win with a fast release - exclusive-class choke is the
  // stricter rule and takes precedence over "let it ring", exactly like
  // a closed hi-hat choking an open one despite the different pitches.
  std::vector<PresetSpec> presets = {
    { "HiHats", 0, {}, {}, /*region_count=*/2,
      /*region_gens=*/{
        { GenSpec{ 43, packKeyRange(42, 42) }, GenSpec{ 57, 5 }, GenSpec{ 38, 0 } },
        { GenSpec{ 43, packKeyRange(46, 46) }, GenSpec{ 57, 5 }, GenSpec{ 38, 0 } },
      } },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "choke_composition_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  InstrumentTrackState state(config, /*solo=*/false, /*muted=*/false, /*track_id=*/0, /*instrument_id=*/0, SphericalPosition{}, SendLevels{});

  state.addVoice(0, instrument->playNote(config, SphericalPosition{}, frequencyForMidiKey(42), 1.0f, 0.8f, 42, SendLevels{}));

  // Same column (0) as the key-42 voice, different identity (46 != 42) -
  // exercises retriggerVoices()'s normal-stopNote() branch - AND shares
  // exclusiveClass 5, so chokeExclusiveClasses() must override it.
  auto voice46 = instrument->playNote(config, SphericalPosition{}, frequencyForMidiKey(46), 1.0f, 0.8f, 46, SendLevels{});
  state.retriggerVoices(0, 46);
  state.chokeExclusiveClasses(*voice46);

  // If only the normal ~1.0s stopNote() had applied (choke not composing
  // correctly), this would still be active at ~371ms - the test is only
  // meaningful because that window is comfortably short of 1.0s.
  bool became_inactive = false;
  for (int i = 0; i < 8 && !became_inactive; i++) {
    state.renderVoices(4096);
    if (!state.isActive()) became_inactive = true;
  }
  CHECK(became_inactive);
}

TEST(fast_release_cascades_through_every_region_of_a_multi_region_group) {
  std::vector<PresetSpec> presets = {
    { "StereoPad", 0, { GenSpec{ 38, 0 } }, {}, /*region_count=*/2 },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "fast_release_group_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  CHECK(voice->isActive());
  CHECK(voice->getChildren().size() == 2);

  voice->fastRelease();

  // If fastRelease() didn't cascade into both region children (only the
  // default TrackState recursion does that - a leaf-only implementation
  // would leave an un-recursed sibling stuck sustaining forever), this
  // would never become inactive within any number of iterations. 1.0s
  // authored release vs. this loop's ~743ms budget also means a
  // genuinely fast release is what's being verified, not just "some"
  // release eventually happening.
  bool became_inactive = false;
  for (int i = 0; i < 8 && !became_inactive; i++) {
    voice->render(4096);
    if (!voice->isActive()) became_inactive = true;
  }
  CHECK(became_inactive);
}

TEST(get_exclusive_classes_reports_the_regions_own_class_or_none) {
  std::vector<PresetSpec> presets = {
    { "NoClass", 0, {}, {} },                        // 0: exclusiveClass left at spec default (0 = none)
    { "WithClass", 0, { GenSpec{ 57, 7 } }, {} },     // 1: exclusiveClass = 7
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "get_exclusive_classes_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);

  auto noClassInstrument = sf.createInstrument(0);
  auto voice0 = noClassInstrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  CHECK(voice0->getExclusiveClasses().empty());

  auto withClassInstrument = sf.createInstrument(1);
  auto voice1 = withClassInstrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  auto classes1 = voice1->getExclusiveClasses();
  CHECK(classes1.size() == 1);
  CHECK(classes1[0] == 7);
}

// ---------------------------------------------------------------------
// Silence-kill threshold (SoundFontVoice::render()) - see
// plans/silence-kill-threshold.md. Uses GenSpec{37, centibels} (SustainVolEnv,
// converted via decibelsToGain(-centibels/10) - see SoundFont.cpp's
// tsf_region_envtosecs()) to put a region's sustain level on a specific
// side of the default -60dB floor, and GenSpec{38, 0} (ReleaseVolEnv = 0
// timecents = 1.0s, the same convention as the retrigger-cutoff tests
// above) for a release long enough that "already inactive" can only mean
// "freed early by the threshold", never "finished naturally".
// ---------------------------------------------------------------------

TEST(sf2_voice_stays_active_while_held_even_below_the_silence_floor) {
  std::vector<PresetSpec> presets = {
    // SustainVolEnv=800 centibels -> decibelsToGain(-80) ~= -80dB, well
    // below the default -60dB floor.
    { "QuietSustain", 0, { GenSpec{ 37, 800 }, GenSpec{ 38, 0 } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "silence_floor_held_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  CHECK(voice->isActive());

  // Held (never stopNote()'d) - must never be killed regardless of how
  // quiet its sustain level is; the threshold only ever applies in
  // RELEASE.
  for (int i = 0; i < 8; i++) {
    voice->render(4096);
    CHECK(voice->isActive());
  }
}

TEST(sf2_voice_releasing_below_the_silence_floor_is_freed_early) {
  std::vector<PresetSpec> presets = {
    { "QuietSustain", 0, { GenSpec{ 37, 800 }, GenSpec{ 38, 0 } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "silence_floor_release_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  voice->stopNote();

  // Already below the floor at the moment release starts - must be freed
  // within a handful of blocks, nowhere close to the full 1.0s authored
  // release (stopNote() alone doesn't jump the level - only starts the
  // release timer - so without the threshold this would keep "releasing"
  // inaudibly for the full second).
  bool became_inactive = false;
  for (int i = 0; i < 8 && !became_inactive; i++) {
    voice->render(4096); // 8*4096 ~= 743ms, comfortably short of 1.0s
    if (!voice->isActive()) became_inactive = true;
  }
  CHECK(became_inactive);
}

TEST(sf2_voice_releasing_above_the_silence_floor_is_not_freed_early) {
  std::vector<PresetSpec> presets = {
    // SustainVolEnv=0 -> full level (0dB, decibelsToGain(0) == 1.0), well
    // above the floor.
    { "FullSustain", 0, { GenSpec{ 37, 0 }, GenSpec{ 38, 0 } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "silence_floor_control_fixture.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100);
  auto instrument = sf.createInstrument(0);

  auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 60, SendLevels{});
  voice->stopNote();

  // Shortly after stopNote(), on a 1.0s release starting at full level,
  // still far too early to have crossed -60dB - must still be genuinely
  // active (naturally releasing), not already finished.
  voice->render(4096); // ~93ms
  CHECK(voice->isActive());
}

// Percussion key-offset mechanism (SoundFont.cpp's applyPercussionOffset(),
// bank 128 only) - isolated from the floor reflection (Phase 3) by
// disabling it explicitly, so these tests are only sensitive to the
// offset mechanism itself. All render through ChannelConfiguration order
// 1 (FOA) and read the Y (ACN1) channel's sign/magnitude directly -
// computeAmbisonicGains's Y gain is sinf(azimuth)*cos(elevation), so a
// positive resolved azimuth reads as a positive Y channel and vice versa,
// without needing a full stereo decode or reaching into voice internals.
namespace {
float channelPeak(VoiceState & voice, int channel, int frames) {
  auto data = voice.render(frames);
  auto * c = data.getChannelData(channel);
  float peak = 0.0f;
  for (int i = 0; i < frames; i++) if (std::fabs(c[i]) > std::fabs(peak)) peak = c[i];
  return peak;
}
float yChannelPeak(VoiceState & voice, int frames) { return channelPeak(voice, 1, frames); }
// Y/W at the sample where W (the reference channel - kAmbisonicReferenceGain,
// always 1 for any real direction, regardless of azimuth) peaks - this
// ratio is exactly sin(azimuth)*cos(elevation), independent of the dry
// signal's own amplitude/phase at that instant. Needed to compare *two
// different MIDI keys* (different pitch -> different dry-signal phase
// within the same short window, so a raw peak-to-peak comparison isn't
// meaningful even when the resolved *position* is identical).
float yToWRatioAtWPeak(VoiceState & voice, int frames) {
  auto data = voice.render(frames);
  auto * w = data.getChannelData(0);
  auto * y = data.getChannelData(1);
  int best = 0;
  for (int i = 1; i < frames; i++) if (std::fabs(w[i]) > std::fabs(w[best])) best = i;
  return w[best] != 0.0f ? y[best] / w[best] : 0.0f;
}
// Z (ACN2, elevation-driven) - unaffected by anything that only ever
// touches azimuth (the region's own native SF2 pan, adjustPositionForPan()
// in this same file), so a jitter/elevation comparison via this channel
// stays valid regardless of that unrelated code path's own behavior.
float zChannelPeak(VoiceState & voice, int frames) { return channelPeak(voice, 2, frames); }
}

// These tests all compare *two* renders of the same key/region (extent on
// vs. off, or one key vs. another) rather than asserting an absolute
// resolved azimuth sign - SoundFontVoice's ctor also folds the region's
// own native SF2 pan into azimuth (adjustPositionForPan(), unrelated to
// applyPercussionOffset()), and every region in these minimal fixtures
// shares the same (unset -> default) pan value. Comparing two renders of
// the *same* region cancels that shared contribution out either way,
// isolating exactly what applyPercussionOffset() itself adds - the tests
// remain valid regardless of what that unrelated pan path does.
TEST(sf2_percussion_offset_hihat_reads_positive_azimuth_at_player_distance) {
  std::vector<PresetSpec> presets = { { "Kit", 0, {}, {}, 1, {}, 128 } };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "percussion_offset_hihat_player.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  auto instrument = sf.createInstrument(0);

  // Key 42 = Closed Hi-Hat, tabulated at u=0.55 (positive - "player"
  // side). Distance 0.5 (<= 1 -> player perspective).
  SphericalPosition position_offset{ 0.0f, 0.0f, 0.5f, 1.2f };
  SphericalPosition position_base{ 0.0f, 0.0f, 0.5f, 0.0f }; // extent 0 - offset mechanism inert

  auto voice_offset = instrument->playNote(config, position_offset, frequencyForMidiKey(42), 1.0f, 0.8f, 42, SendLevels{});
  auto voice_base = instrument->playNote(config, position_base, frequencyForMidiKey(42), 1.0f, 0.8f, 42, SendLevels{});
  float delta = yChannelPeak(*voice_offset, 64) - yChannelPeak(*voice_base, 64);
  CHECK(delta > 0.01f);
}

TEST(sf2_percussion_offset_mirrors_at_audience_distance) {
  std::vector<PresetSpec> presets = { { "Kit", 0, {}, {}, 1, {}, 128 } };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "percussion_offset_hihat_audience.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  auto instrument = sf.createInstrument(0);

  // Same key as above, distance 1.5 (> 1 -> audience perspective) - the
  // offset's own contribution to the Y channel must flip sign relative
  // to the player-distance case above.
  SphericalPosition position_offset{ 0.0f, 0.0f, 1.5f, 1.2f };
  SphericalPosition position_base{ 0.0f, 0.0f, 1.5f, 0.0f };

  auto voice_offset = instrument->playNote(config, position_offset, frequencyForMidiKey(42), 1.0f, 0.8f, 42, SendLevels{});
  auto voice_base = instrument->playNote(config, position_base, frequencyForMidiKey(42), 1.0f, 0.8f, 42, SendLevels{});
  float delta = yChannelPeak(*voice_offset, 64) - yChannelPeak(*voice_base, 64);
  CHECK(delta < -0.01f);
}

TEST(sf2_percussion_offset_zero_extent_collapses_to_point_source) {
  std::vector<PresetSpec> presets = { { "Kit", 0, {}, {}, 1, {}, 128 } };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "percussion_offset_zero_extent.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  auto instrument = sf.createInstrument(0);

  // extent 0 - regardless of the table entry, there is nothing to offset
  // within, so two very differently-tabulated keys (42 and 49, table
  // entries u=0.55 and u=-0.7) must resolve to the *same* Y channel -
  // any difference would mean the table is still being consulted
  // despite extent being 0. Both keys match the same single region in
  // this fixture, so nothing else differs between them.
  SphericalPosition position{ 0.0f, 0.0f, 0.5f, 0.0f };
  auto voice_42 = instrument->playNote(config, position, frequencyForMidiKey(42), 1.0f, 0.8f, 42, SendLevels{});
  auto voice_49 = instrument->playNote(config, position, frequencyForMidiKey(49), 1.0f, 0.8f, 49, SendLevels{});
  CHECK_NEAR(yChannelPeak(*voice_42, 64), yChannelPeak(*voice_49, 64), 0.0001f);
}

TEST(sf2_percussion_offset_never_applies_to_a_non_percussion_bank) {
  // Program 40 (Violin) - bank 0, deliberately outside both the
  // percussion bank (128) and the pitched-arc family (piano 0-7, harp
  // 46), so this fixture exercises neither of the new mechanisms.
  std::vector<PresetSpec> presets = { { "Violin", 40, {}, {} } };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "percussion_offset_non_percussion.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  auto instrument = sf.createInstrument(0);

  // Same two keys/extent as above, but bank 0/program 40 - not a GM
  // percussion kit and not in the pitched-arc family - so neither
  // mechanism ever applies regardless of key; the resolved position is
  // identical for both. Compared via yToWRatioAtWPeak() (not a raw peak)
  // because this fixture's instrument isn't percussion/arc-eligible, so
  // it still goes through the region's own native SF2 pan
  // (adjustPositionForPan(), skip_native_pan stays false here) -
  // producing a nonzero resolved azimuth, unlike the zero-extent case
  // above (which skips native pan entirely and lands on an *exact* zero
  // azimuth, making a raw peak comparison safe there but not here) - a
  // raw peak comparison across two different pitches would otherwise be
  // confounded by their differing dry-signal phase within the window.
  SphericalPosition position{ 0.0f, 0.0f, 0.5f, 1.2f };
  auto voice_42 = instrument->playNote(config, position, frequencyForMidiKey(42), 1.0f, 0.8f, 42, SendLevels{});
  auto voice_49 = instrument->playNote(config, position, frequencyForMidiKey(49), 1.0f, 0.8f, 49, SendLevels{});
  CHECK_NEAR(yToWRatioAtWPeak(*voice_42, 64), yToWRatioAtWPeak(*voice_49, 64), 0.0001f);
}

TEST(sf2_percussion_offset_jitter_is_deterministic_and_varies_per_coordinate) {
  std::vector<PresetSpec> presets = { { "Kit", 0, {}, {}, 1, {}, 128 } };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "percussion_offset_jitter.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  SphericalPosition position{ 0.0f, 0.0f, 0.5f, 1.2f };

  // Two freshly-constructed instruments hitting the same key, at the same
  // NoteCoordinate, must produce bit-identical jitter - a full re-render
  // from scratch reproduces exactly, and so does replaying the very same
  // authored note a second time (e.g. a pattern loop) - unlike the old
  // per-instance jitter counter this replaced, which instead advanced once
  // per hit regardless of position, so a second call always drew a
  // different value even for what should have been the identical note.
  // Compared via the Z (elevation) channel - see zChannelPeak()'s own
  // comment above.
  NoteCoordinate coord_row0(0, 0, 0);
  auto instrument_a = sf.createInstrument(0);
  auto voice_a = instrument_a->playNote(config, position, frequencyForMidiKey(49), 1.0f, 0.8f, 49, SendLevels{}, coord_row0);
  float peak_a = zChannelPeak(*voice_a, 64);

  auto instrument_b = sf.createInstrument(0);
  auto voice_b = instrument_b->playNote(config, position, frequencyForMidiKey(49), 1.0f, 0.8f, 49, SendLevels{}, coord_row0);
  float peak_b = zChannelPeak(*voice_b, 64);

  CHECK_NEAR(peak_a, peak_b, 0.0001f);

  // A different coordinate (e.g. a different pattern row) must not land
  // on the exact same offset - two genuinely distinct hits of the same
  // key aren't pinned to an identical point.
  NoteCoordinate coord_row1(0, 1, 0);
  auto voice_c = instrument_a->playNote(config, position, frequencyForMidiKey(49), 1.0f, 0.8f, 49, SendLevels{}, coord_row1);
  float peak_c = zChannelPeak(*voice_c, 64);
  CHECK(std::fabs(peak_c - peak_a) > 0.0001f);
}

// Pitched arc (SoundFontInstrument::applyPitchedArcOffset(), the piano
// family/harp only - isPitchedArcFamily()). Program 0 (Acoustic Grand
// Piano) with an explicit 60-84 key range so the arc's own endpoints and
// midpoint land on round key numbers - key 60 is u=-1 (lowest mapped
// key), key 84 is u=+1 (highest), key 72 is the exact midpoint (u=0).
TEST(sf2_pitched_arc_opposite_ends_shift_opposite_directions) {
  std::vector<PresetSpec> presets = {
    { "Piano", 0, { GenSpec{ 43, packKeyRange(60, 84) } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "pitched_arc_ends.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  auto instrument = sf.createInstrument(0);

  // Player perspective (distance <= 1) - a real extent to arc across.
  SphericalPosition position{ 0.0f, 0.0f, 0.5f, 1.5f };
  auto voice_low = instrument->playNote(config, position, frequencyForMidiKey(60), 1.0f, 0.8f, 60, SendLevels{});
  auto voice_high = instrument->playNote(config, position, frequencyForMidiKey(84), 1.0f, 0.8f, 84, SendLevels{});

  float ratio_low = yToWRatioAtWPeak(*voice_low, 64);
  float ratio_high = yToWRatioAtWPeak(*voice_high, 64);
  // Opposite signs (one end of the arc reads left, the other right) and
  // roughly the same magnitude (u = -1 and +1 are symmetric).
  CHECK(ratio_low * ratio_high < 0.0f);
  CHECK_NEAR(std::fabs(ratio_low), std::fabs(ratio_high), 0.05f);
}

TEST(sf2_pitched_arc_midpoint_key_is_centered) {
  std::vector<PresetSpec> presets = {
    { "Piano", 0, { GenSpec{ 43, packKeyRange(60, 84) } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "pitched_arc_midpoint.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  auto instrument = sf.createInstrument(0);

  // Key 72 is the exact midpoint of 60-84 -> u = 0 -> azimuth stays
  // exactly at the input (0), reading as an exact zero Y channel (not
  // just "near" it - u = 0 makes the offset an exact no-op, same
  // "multiplying by an exact zero gain" reasoning as the zero-extent
  // percussion test above).
  SphericalPosition position{ 0.0f, 0.0f, 0.5f, 1.5f };
  auto voice_mid = instrument->playNote(config, position, frequencyForMidiKey(72), 1.0f, 0.8f, 72, SendLevels{});
  CHECK_NEAR(yChannelPeak(*voice_mid, 64), 0.0f, 0.0001f);
}

TEST(sf2_pitched_arc_mirrors_at_audience_distance) {
  std::vector<PresetSpec> presets = {
    { "Piano", 0, { GenSpec{ 43, packKeyRange(60, 84) } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "pitched_arc_mirror.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  auto instrument = sf.createInstrument(0);

  // Same highest key (84, u = +1) at player (<= 1) vs audience (> 1)
  // distance - the arc's own direction must flip, same mirror
  // convention as the percussion table.
  SphericalPosition position_player{ 0.0f, 0.0f, 0.5f, 1.5f };
  SphericalPosition position_audience{ 0.0f, 0.0f, 1.5f, 1.5f };
  auto voice_player = instrument->playNote(config, position_player, frequencyForMidiKey(84), 1.0f, 0.8f, 84, SendLevels{});
  auto voice_audience = instrument->playNote(config, position_audience, frequencyForMidiKey(84), 1.0f, 0.8f, 84, SendLevels{});

  float ratio_player = yToWRatioAtWPeak(*voice_player, 64);
  float ratio_audience = yToWRatioAtWPeak(*voice_audience, 64);
  CHECK(ratio_player * ratio_audience < 0.0f);
}

// Newly-added arc-family instruments (glockenspiel/vibraphone/marimba/
// xylophone/tubular bells/timpani, GM programs 9/11/12/13/14/47) - one
// representative (Marimba, program 12) confirming both halves of the
// extension: getDefaultExtent() returns its new tabulated span, and the
// arc actually resolves opposite-key azimuths in opposite directions
// (same check as the pre-existing piano test above).
TEST(sf2_pitched_arc_covers_newly_added_mallet_family) {
  std::vector<PresetSpec> presets = {
    { "Marimba", 12, { GenSpec{ 43, packKeyRange(60, 84) } }, {} },
  };
  auto path = (std::filesystem::path(TESTS_SCRATCH_DIR) / "pitched_arc_marimba.sf2").string();
  writeMinimalSf2(path, presets);

  SoundFont sf(path);
  ChannelConfiguration config(44100, 1);
  config.setFloorReflectionEnabled(false);
  auto instrument = sf.createInstrument(0);

  CHECK_NEAR(instrument->getDefaultExtent(), 1.2f, 0.0001f);

  SphericalPosition position{ 0.0f, 0.0f, 0.5f, 1.2f };
  auto voice_low = instrument->playNote(config, position, frequencyForMidiKey(60), 1.0f, 0.8f, 60, SendLevels{});
  auto voice_high = instrument->playNote(config, position, frequencyForMidiKey(84), 1.0f, 0.8f, 84, SendLevels{});

  float ratio_low = yToWRatioAtWPeak(*voice_low, 64);
  float ratio_high = yToWRatioAtWPeak(*voice_high, 64);
  CHECK(ratio_low * ratio_high < 0.0f);
}
