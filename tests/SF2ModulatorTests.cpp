#include "TestFramework.h"

#include "../SF2Modulator.h"
#include "../SoundFont.h"
#include "../ChannelConfiguration.h"
#include "../SphericalPosition.h"
#include "../SendLevels.h"
#include "../TrackState.h"
#include "../InstrumentTrackState.h"
#include "../RenderContext.h"
#include "../Track.h"
#include "../SampleData.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
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
// render()) and SF2 voice lifecycle. Exercises the real public
// SoundFont/Instrument/TrackState API, not any internal seam - the
// private tsf_hydra_*/tsf_region types have no test-only exposure, by
// design.
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

      // phdr: one zone (this preset's own instrument), bank 0.
      appendName20(phdr_bytes, p.name);
      appendU16(phdr_bytes, p.program);
      appendU16(phdr_bytes, 0); // bank
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
  float mainChannelDifference(const SampleData & low, const SampleData & high) {
    auto n = low.numberOfFrames();
    if (n != high.numberOfFrames() || !low.hasChannel(Channel::Main) || !high.hasChannel(Channel::Main)) return -1.0f;
    auto a = low.getChannelData(0), b = high.getChannelData(0);
    float total = 0.0f;
    for (int i = 0; i < n; i++) total += std::fabs(a[i] - b[i]);
    return total;
  }

}

TEST(sf2_channel_pressure_reaches_every_region_in_a_multi_region_group) {
  // Real GM patches commonly ship more than one matching region per note
  // (stereo L/R sample pairs, velocity layers) - SoundFontInstrument::
  // playNote() then returns a group TrackState wrapping several
  // SoundFontVoice children instead of a single voice directly.
  // TrackState::applyChannelPressure()'s default must recurse into
  // children (like applyAftertouch already does) for the pressure to
  // ever reach those children - a regression test for exactly that,
  // using a 2-region Pad preset (both regions covering the full key/vel
  // range, so both always match) with its own explicitly authored
  // channel-pressure -> filter-cutoff modulator (not relying on any
  // GM-family default heuristic - there isn't one; only a file's own
  // real modulators are ever honored).
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

  auto voice_low = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 0.0f, 60, SendLevels{});
  voice_low->applyChannelPressure(0.0f);
  auto low = voice_low->render(8192);

  auto voice_high = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 0.0f, 60, SendLevels{});
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
  auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 0.0f, 60, SendLevels{});
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
  // <genericInstrument name="Cello"><oscilator .../></genericInstrument>)
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
  // having both killNote() and stopNote() call TrackState::killNote() on
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

  auto voice = lead_instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 0.0f, 60, SendLevels{});
  auto modulator = modulator_instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 1.0f, 0.0f, 60, SendLevels{});
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
  // Both tests above drive SoundFontVoice/the group TrackState directly via
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
                              SphericalPosition{}, /*portamento=*/-1.0f, SendLevels{});
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
  // true. For a multi-region group (TrackState::isActive() ORs over
  // children - real GM patches with stereo/velocity-layered regions
  // commonly have per-region envelopes that finish releasing at different
  // times), that guard only proves *some* child is still active, not that
  // *every* child is - the base TrackState::stopNote() recurses into every
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
  auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 0.0f, 60, SendLevels{});
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
  // non-overriding TrackState group wrapping several SoundFontVoice
  // children instead of a single voice directly - unlike
  // sf2_looping_voice_becomes_inactive_after_stop_note above, which only
  // ever exercised a single bare SoundFontVoice. Group TrackState::
  // isActive() ORs over children and stopVoices()/TrackState::stopNote()
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
  auto voice = instrument->playNote(config, SphericalPosition{}, 440.0f, 1.0f, 0.8f, 0.0f, 60, SendLevels{});
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
