#ifndef _INSTRUMENTPROVIDER_H_
#define _INSTRUMENTPROVIDER_H_

#include "Instrument.h"
#include "SoundFont.h"
#include "Oscillator.h"
#include "GmInstrumentTable.h"

#include <string>
#include <string_view>
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
      // Every GM bank-0 program, under its canonical taxonomy path - see
      // GmInstrumentTable.h/docs/instrument-paths.md, the single source of
      // truth this table is generated from. createInstrumentByProgram()
      // returns nullptr for a program this particular font doesn't provide
      // (see its own doc comment) - skipped rather than registering an
      // empty instrument, exactly the "no provider at this path" case
      // resolvePath()'s walk-up is designed to handle.
      for (auto & entry : kGmBank0Table) {
	auto instrument = sf->createInstrumentByProgram(0, entry.program, entry.path);
	if (instrument) registerPath(entry.path, move(instrument));
      }

      // Not a taxonomy path - kit.standard only becomes resolvable once
      // <instrumentMap> support exists (docs/instrument-paths.md's bank-128
      // section). This is only the (bank,program) lookup fix: the old
      // createInstrument(160, "Percussion") read 160 as a raw index into the
      // font's global sorted-by-(bank,program) preset array, not a bank-128
      // program number - on a font carrying extra banks between 0 and 128
      // (FluidR3_GM.sf2/default-GM.sf2 both do; TimGM6mb.sf2 doesn't have
      // enough presets for index 160 to exist at all), that landed on a kit
      // variant or nothing, never the standard kit at (128, 0).
      // Unlike every createInstrumentByProgram() call above, this one isn't
      // guarded by the same `if (instrument)` loop shape since it's a lone
      // call, not iterating a table - same nullptr contract though.
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

  // Exact-string lookup only - no default-instrument fallback (unlike the
  // old getInstrumentByName(), removed because that fallback made it
  // impossible to tell "found" from "substituted the default" one layer up,
  // which GenericInstrument::prepare()'s two-step resolution needs to do).
  // Covers native names and any not-yet-migrated literal string a song
  // might still hold; taxonomy paths are never registered here (see
  // registerPath()) so this alone can't resolve one.
  std::shared_ptr<Instrument> tryGetByLiteralName(const std::string & name) const {
    auto it = instruments_by_name.find(name);
    return it != instruments_by_name.end() ? it->second : nullptr;
  }

  // Registers `instrument` under the dotted taxonomy path `path` -
  // callable by any provider, not just loadSoundFont()'s own GM-table loop
  // above (see docs/instrument-paths.md's forward-compatibility notes: a
  // future non-GM-mapped font registers its own presets at their own paths
  // the same way). Last registration wins on a collision, a plain
  // overwrite - provider *load order* already expresses priority (load the
  // system font, then a user's font), so the registry itself doesn't need
  // a separate priority scheme; see the doc's own "Collisions" note.
  void registerPath(std::string path, std::shared_ptr<Instrument> instrument) {
    paths_by_taxonomy_path[std::move(path)] = std::move(instrument);
  }

  // Resolves a dotted path per docs/instrument-paths.md's two-pass
  // algorithm: walk up the literal request first (the exact path, then
  // progressively shorter dotted prefixes), and only once that's
  // exhausted, redirect through the bare-root "defaults for general
  // requests" table - never the other order, since an exact/deeper
  // registration must always win over a curated default. A matched
  // default's target is looked up via walkUp(), not a second recursive
  // resolvePath() - deliberately not chaining into a second defaults pass.
  // No entry in kGmPathDefaults is itself the target of another (checked),
  // so nothing needs it today, and allowing it would reopen a real
  // divide-by-zero-shaped hazard: resolving a target that isn't actually
  // registered (e.g. `kit.standard`, unreachable until <instrumentMap>
  // support exists) would shrink right back down to the same default key
  // that produced it and redirect again, forever - confirmed by a stack
  // overflow before this comment existed. Returns nullptr on a full miss;
  // callers decide their own final fallback (see GenericInstrument::prepare()).
  std::shared_ptr<Instrument> resolvePath(const std::string & path) const {
    if (auto found = walkUp(path)) return found;
    for (std::string_view prefix = path; ; ) {
      for (auto & entry : kGmPathDefaults) {
	if (prefix == entry.request) return walkUp(entry.target);
      }
      auto dot = prefix.rfind('.');
      if (dot == std::string_view::npos) return nullptr;
      prefix = prefix.substr(0, dot);
    }
  }

  std::shared_ptr<Instrument> getDefaultInstrument() const { return default_instrument; }

  const std::unordered_map<std::string, std::shared_ptr<Instrument> > & getInstruments() const { return instruments_by_name; }

protected:
  void addInstrument(std::shared_ptr<Instrument> instrument) {
    instruments_by_name[instrument->getName()] = instrument;
  }

 private:
  std::shared_ptr<Instrument> walkUp(const std::string & path) const {
    for (std::string_view prefix = path; ; ) {
      auto it = paths_by_taxonomy_path.find(std::string(prefix));
      if (it != paths_by_taxonomy_path.end()) return it->second;
      auto dot = prefix.rfind('.');
      if (dot == std::string_view::npos) return nullptr;
      prefix = prefix.substr(0, dot);
    }
  }

  std::unordered_map<std::string, std::shared_ptr<Instrument> > instruments_by_name;
  std::unordered_map<std::string, std::shared_ptr<Instrument> > paths_by_taxonomy_path;
  std::shared_ptr<Instrument> default_instrument;
};

#endif
