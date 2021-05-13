#ifndef _SOUNDFONT_H_
#define _SOUNDFONT_H_

#include "InstrumentSet.h"

#include <string>

class SoundFontFile;

class SoundFont : public InstrumentSet {
 public:  
  explicit SoundFont(std::string _filename) : filename(_filename) {
    openFile();
  }

  std::unique_ptr<Instrument> createInstrument(size_t preset, size_t fixedMidiKey = 0, const char * name = 0) override;
  
  std::vector<std::unique_ptr<Instrument> > createAll() override;
  
protected:
  void openFile();

private:
  std::string filename;
  std::shared_ptr<SoundFontFile> sf;
};

#endif
