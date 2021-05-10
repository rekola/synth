#ifndef _INSTRUMENTPROVIDER_H_
#define _INSTRUMENTPROVIDER_H_

#include "Instrument.h"
#include "SoundFont.h"

#include <string>
#include <memory>
#include <unordered_map>

class InstrumentProvider {
 public:
  InstrumentProvider() { }

  void loadSoundFont(std::string filename) {
    auto sf = std::make_unique<SoundFont>(filename);
    auto instruments = sf->createAll();
    for (auto & instrument : instruments) {
      auto & name = instrument->getName();
      if (!name.empty()) {
	instruments_by_name[name] = std::move(instrument);
      }
    }
  }
  
  std::shared_ptr<Instrument> getInstrumentByName(std::string name) const {
    auto it = instruments_by_name.find(name);
    if (it != instruments_by_name.end()) {
      return it->second;
    } else {
      return std::shared_ptr<Instrument>(0);
    }
  }

  
  
 private:
  std::unordered_map<std::string, std::shared_ptr<Instrument> > instruments_by_name;
};

#endif
