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

// 27 High Q
// 28 Slap
// 29 Stratch Push
// 30 Stratch Pull
// 31 Sticks
// 32 Square Click
// 33 Metronome Click
// 34 Metronome Bell
    addInstrument(sf->createInstrument(160, 35, "Acoustic Bass Drum"));
    addInstrument(sf->createInstrument(160, 36, "Electric Bass Drum"));
// 37 Side Stick
    addInstrument(sf->createInstrument(160, 38, "Acoustic Snare"));
// 39 Hand Clap
    addInstrument(sf->createInstrument(160, 40, "Electric Snare"));
// 41 Low Floor Tom
    addInstrument(sf->createInstrument(160, 42, "Closed Hi-hat"));
// 43 High Floor Tom
// 44 Pedal Hi-hat
// 45 Low Tom
    addInstrument(sf->createInstrument(160, 46, "Open Hi-hat"));
// 47 Low-Mid Tom
// 48 Hi-Mid Tom
// 49 Crash Cymbal 1
// 50 High Tom
// 51 Ride Cymbal 1
// 52 Chinese Cymbal
// 53 Ride Bell
    addInstrument(sf->createInstrument(160, 54, "Tambourine"));
// 55 Splash Cymbal
// 56 Cowbell
// 57 Crash Cymbal 2
// 58 Vibra Slap
// 59 Ride Cymbal 2
// 60 High Bongo
    addInstrument(sf->createInstrument(160, 61, "Low Bongo"));
// 62 Mute High Conga
// 63 Open High Conga
// 64 Low Conga
// 65 High Timbale
// 66 Low Timbale
// 67 High Agogô
// 68 Low Agogô
// 69 Cabasa
    addInstrument(sf->createInstrument(160, 70, "Maracas"));
// 71 Short Whistle
// 72 Long Whistle
// 73 Short Guiro
// 74 Long Guiro
// 75 Claves
// 76 High Woodblock
// 77 Low Woodblock
// 78 Mute Cuica
// 79 Open Cuica
// 80 Mute Triangle
// 81 Open Triangle
    addInstrument(sf->createInstrument(160, 82, "Shaker"));
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
