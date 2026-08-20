#ifndef _TESTS_SF2FIXTURE_H_
#define _TESTS_SF2FIXTURE_H_

// Builds a minimal synthetic .sf2 file at test time - shared by any test
// that needs a real (if tiny) SoundFont to exercise SoundFontFile's parser
// against, rather than a checked-in binary fixture (tests/fixtures/ is
// otherwise 100% text/XML). Originally SF2ModulatorTests.cpp's own private
// helper; extracted here once InstrumentResolverTests.cpp needed the same
// machinery (a deliberate (bank,program) gap between presets) rather than
// duplicate ~150 lines of RIFF-chunk writing.

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace sf2fixture {

using Bytes = std::vector<uint8_t>;

inline void appendU8(Bytes & b, uint8_t v) { b.push_back(v); }
inline void appendU16(Bytes & b, uint16_t v) { appendU8(b, static_cast<uint8_t>(v & 0xFF)); appendU8(b, static_cast<uint8_t>((v >> 8) & 0xFF)); }
inline void appendI16(Bytes & b, int16_t v) { appendU16(b, static_cast<uint16_t>(v)); }
inline void appendU32(Bytes & b, uint32_t v) { appendU16(b, static_cast<uint16_t>(v & 0xFFFF)); appendU16(b, static_cast<uint16_t>((v >> 16) & 0xFFFF)); }
inline void appendFourCC(Bytes & b, const char * cc) { for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(cc[i])); }
inline void appendName20(Bytes & b, const std::string & name) {
  for (size_t i = 0; i < 20; i++) b.push_back(i < name.size() ? static_cast<uint8_t>(name[i]) : 0);
}

// Wraps `payload` as a plain (non-LIST/RIFF) chunk: fourcc + size + payload.
inline void appendChunk(Bytes & out, const char * fourcc, const Bytes & payload) {
  appendFourCC(out, fourcc);
  appendU32(out, static_cast<uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

// Wraps `payload` as a LIST chunk of the given 4-byte type ("pdta"/"sdta").
inline void appendListChunk(Bytes & out, const char * listType, const Bytes & payload) {
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
inline int16_t packKeyRange(uint8_t lo, uint8_t hi) {
  return static_cast<int16_t>(static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8));
}

// MIDI-key-number -> frequency, the same 12-TET formula Tuner::
// getFrequency(Tuning::PERCUSSION, ...) uses - lets exclusive-class
// tests select a specific region by key the same way a real percussion
// note-on would, via SoundFontInstrument::playNote()'s own
// frequency->midiKey round trip.
inline float frequencyForMidiKey(int key) {
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
// generators/modulators/loop points listed in `presets` differ. Presets
// need not be contiguous in (bank, program) - a deliberate gap is just a
// PresetSpec list that skips a program number, letting a resolution test
// assert a path binds to the same logical patch regardless of where the
// gap shifts its position in the file's own sorted-by-(bank,program)
// preset array (see SoundFont::createInstrumentByProgram()'s own doc
// comment for why that distinction matters).
inline void writeMinimalSf2(const std::string & path, const std::vector<PresetSpec> & presets) {
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

} // namespace sf2fixture

#endif
