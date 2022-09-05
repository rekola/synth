#ifndef _INSTRUMENTPROVIDER_H_
#define _INSTRUMENTPROVIDER_H_

#include "Instrument.h"
#include "SoundFont.h"
#include "Oscilator.h"

#include <string>
#include <memory>
#include <unordered_map>

class InstrumentProvider {
 public:
  InstrumentProvider() {
    auto epiano = std::make_shared<Oscilator>(WaveformType::SAW);
    epiano->setName("Electric Piano");
    addInstrument(epiano);

    default_instrument = epiano;
  }

  void loadSoundFont(std::string filename, bool as_midi_default = true) {
    auto sf = std::make_unique<SoundFont>(std::move(filename));

    if (as_midi_default) {
      // Piano
      addInstrument(sf->createInstrument(0, "Piano"));
      addInstrument(sf->createInstrument(0, "Acoustic Grand Piano"));
      
      addInstrument(sf->createInstrument(1, "Bright Acoustic Piano"));

      addInstrument(sf->createInstrument(2, "Electric Grand Piano"));
      addInstrument(sf->createInstrument(2, "Electric Piano"));

      addInstrument(sf->createInstrument(3, "Honky-tonk Piano"));
      addInstrument(sf->createInstrument(4, "Rhodes Piano"));
      addInstrument(sf->createInstrument(5, "Chorused Piano"));
      addInstrument(sf->createInstrument(6, "Harpsichord"));
      addInstrument(sf->createInstrument(7, "Clavinet"));
      
      addInstrument(sf->createInstrument(24, "Acoustic Guitar (nylon)"));
      addInstrument(sf->createInstrument(24, "Acoustic Guitar"));

      addInstrument(sf->createInstrument(25, "Acoustic Guitar (steel)"));

      addInstrument(sf->createInstrument(34, "Electric Bass (finger)"));

      addInstrument(sf->createInstrument(40, "Violin"));
      addInstrument(sf->createInstrument(40, "Viola"));      
      addInstrument(sf->createInstrument(42, "Cello"));
      addInstrument(sf->createInstrument(45, "Pizzicato Strings"));
      addInstrument(sf->createInstrument(46, "Orchestral Harp"));

      addInstrument(sf->createInstrument(62, "Synth Brass 1"));
      addInstrument(sf->createInstrument(63, "Synth Brass 2"));

      addInstrument(sf->createInstrument(88, "Pad 1 (new age)"));
      addInstrument(sf->createInstrument(89, "Pad 2 (warm)"));
      addInstrument(sf->createInstrument(90, "Pad 3 (polysynth)"));
      addInstrument(sf->createInstrument(91, "Pad 4 (choir)"));
      addInstrument(sf->createInstrument(92, "Pad 5 (bowed)"));
      addInstrument(sf->createInstrument(93, "Pad 6 (metallic)"));
      addInstrument(sf->createInstrument(94, "Pad 7 (halo)"));
      addInstrument(sf->createInstrument(95, "Pad 8 (sweep)"));

      addInstrument(sf->createInstrument(160, "Percussion"));
    } else {
      auto instruments = sf->createAll();
      for (auto & instrument : instruments) {
	if (!instrument->getName().empty()) {
	  addInstrument(move(instrument));
	}
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
