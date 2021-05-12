
#ifndef _INSTRUMENTPROVIDER_H_
#define _INSTRUMENTPROVIDER_H_

#include "Instrument.h"
#include "SoundFont.h"
#include "SubtractiveInstrument.h"
#include "Filter.h"

#include <string>
#include <memory>
#include <unordered_map>

class InstrumentProvider {
 public:
  InstrumentProvider() {
    auto epiano = std::make_shared<SubtractiveInstrument>(WaveformType::SAW);
    epiano->setName("Electric Piano");
    epiano->setAmpEnvelope(Envelope(0.0f, 10 * 20 / 255.0f, 0.0f, 0.0));
    epiano->addEffect(std::make_unique<Filter>(63 / 255.0f, 128 / 63.0f, false));
    addInstrument(epiano);

    default_instrument = epiano;
  }

  void loadSoundFont(std::string filename) {
    auto sf = std::make_unique<SoundFont>(filename);
    auto instruments = sf->createAll();
    for (auto & instrument : instruments) {
      if (!instrument->getName().empty()) {
	addInstrument(move(instrument));
      }
    }
  }
  
  std::shared_ptr<Instrument> getInstrumentByName(std::string name) const {
    auto it = instruments_by_name.find(name);
    if (it != instruments_by_name.end()) {
      return it->second;
    } else {
      return default_instrument;
    }
  }

protected:
  void addInstrument(std::shared_ptr<Instrument> instrument) {
    instruments_by_name[instrument->getName()] = instrument;
  }
  
 private:
  std::unordered_map<std::string, std::shared_ptr<Instrument> > instruments_by_name;
  std::shared_ptr<Instrument> default_instrument;
};

#endif
