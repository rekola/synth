#ifndef _INSTRUMENTPROVIDER_H_
#define _INSTRUMENTPROVIDER_H_

#include "Instrument.h"
#include "SoundFont.h"
#include "Oscillator.h"

#include <string>
#include <memory>
#include <unordered_map>

class InstrumentProvider {
 public:
  InstrumentProvider() {
    auto epiano = std::make_shared<Oscillator>(WaveformType::SAW);
    epiano->setName("Electric Piano");
    addInstrument(epiano);

    default_instrument = epiano;
  }

  void loadSoundFont(std::string filename, bool as_midi_default = true) {
    auto sf = std::make_unique<SoundFont>(std::move(filename));

    if (as_midi_default) {
      // Taxonomy-path registrations for the curated subset of GM programs -
      // see docs/instrument-paths.md for the full bank-0/bank-128 table this
      // is drawn from. Each entry here is a program this codebase has
      // historically given a friendly alias to; the path is what that alias
      // is renamed to (docs/instrument-paths.md's canonical path for that
      // program), not a new/independent choice. Two pairs collapse to a
      // single registration below because both aliases in the pair were
      // already bound to the same program - not a name each alias
      // separately earned: "Electric Piano" has always meant program 2
      // (Electric Grand Piano, not the Rhodes/FM patches at 4/5), and
      // "Viola" has always meant program 40 (Violin, not the real Viola at
      // 41). Renaming preserves that binding rather than quietly
      // repointing either alias to the program its name suggests.
      addInstrument(sf->createInstrument(0, "piano.acoustic.grand"));
      addInstrument(sf->createInstrument(1, "piano.acoustic.grand.bright"));
      addInstrument(sf->createInstrument(2, "piano.electric.grand"));
      addInstrument(sf->createInstrument(3, "piano.acoustic.upright.honkyTonk"));
      addInstrument(sf->createInstrument(4, "piano.electric.tine"));
      addInstrument(sf->createInstrument(5, "piano.electric.fm"));
      addInstrument(sf->createInstrument(6, "keyboard.plucked.harpsichord"));
      addInstrument(sf->createInstrument(7, "keyboard.electric.clavinet"));

      addInstrument(sf->createInstrument(24, "guitar.acoustic.nylon"));
      addInstrument(sf->createInstrument(25, "guitar.acoustic.steel"));

      addInstrument(sf->createInstrument(34, "bass.electric.finger"));

      addInstrument(sf->createInstrument(40, "string.bowed.violin"));
      addInstrument(sf->createInstrument(42, "string.bowed.cello"));
      addInstrument(sf->createInstrument(45, "string.bowed.ensemble.pizzicato"));
      addInstrument(sf->createInstrument(46, "string.plucked.harp"));

      addInstrument(sf->createInstrument(62, "brass.synth"));
      addInstrument(sf->createInstrument(63, "brass.synth.soft"));

      addInstrument(sf->createInstrument(87, "lead.bassLead")); // bass and lead or solo lead or sometimes mistakenly called "brass and lead"

      addInstrument(sf->createInstrument(88, "pad.newAge"));
      addInstrument(sf->createInstrument(89, "pad.warm"));
      addInstrument(sf->createInstrument(90, "pad.poly"));
      addInstrument(sf->createInstrument(91, "pad.choir"));
      addInstrument(sf->createInstrument(92, "pad.bowed"));
      addInstrument(sf->createInstrument(93, "pad.metallic"));
      addInstrument(sf->createInstrument(94, "pad.halo"));
      addInstrument(sf->createInstrument(95, "pad.sweep"));

      // Not a taxonomy path - kit.standard only becomes resolvable once
      // <instrumentMap> support exists (docs/instrument-paths.md's bank-128
      // section). This is only the (bank,program) lookup fix: the old
      // createInstrument(160, "Percussion") read 160 as a raw index into the
      // font's global sorted-by-(bank,program) preset array, not a bank-128
      // program number - on a font carrying extra banks between 0 and 128
      // (FluidR3_GM.sf2/default-GM.sf2 both do; TimGM6mb.sf2 doesn't have
      // enough presets for index 160 to exist at all), that landed on a kit
      // variant or nothing, never the standard kit at (128, 0).
      // Unlike every createInstrument(size_t, ...) call above,
      // createInstrumentByProgram() can genuinely return nullptr (no preset
      // at that (bank, program) in this font) - addInstrument() assumes a
      // real instrument, so this is guarded explicitly rather than handed a
      // null shared_ptr.
      auto percussion = sf->createInstrumentByProgram(128, 0, "Percussion");
      if (percussion) addInstrument(move(percussion));
    }

    // Register every preset under its own native name too (e.g.
    // "Glockenspiel") - so any GM patch is available from a song even
    // without a curated path above, without overwriting one already
    // registered. Namespaced with a "native:" prefix - a colon can't appear
    // in a dotted taxonomy path, so a native name can never collide with one
    // - keeping "this is this file's own preset name, not a portable path"
    // visible in the key itself rather than by convention alone.
    for (auto & instrument : sf->createAll()) {
      if (instrument->getName().empty()) continue;
      std::string key = "native:" + instrument->getName();
      if (!instruments_by_name.count(key)) {
	instrument->setName(key);
	addInstrument(move(instrument));
      }
    }
  }
  
  std::shared_ptr<Instrument> getInstrumentByName(const std::string & name) const {
    auto it = instruments_by_name.find(name);
    if (it != instruments_by_name.end()) {
      return it->second;
    } else {
      return default_instrument;
    }
  }

  const std::unordered_map<std::string, std::shared_ptr<Instrument> > & getInstruments() const { return instruments_by_name; }

protected:
  void addInstrument(std::shared_ptr<Instrument> instrument) {
    instruments_by_name[instrument->getName()] = instrument;
  }
  
 private:
  std::unordered_map<std::string, std::shared_ptr<Instrument> > instruments_by_name;
  std::shared_ptr<Instrument> default_instrument;
};

#endif
