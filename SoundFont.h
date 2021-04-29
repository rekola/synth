#ifndef _SOUNDFONT_H_
#define _SOUNDFONT_H_

#include "InstrumentSet.h"

#include <string>

class SoundFontFile;

class SoundFont : public InstrumentSet {
 public:  
  explicit SoundFont(int _samplerate, std::string _filename) : samplerate(_samplerate), filename(_filename) {
    openFile();
  }

  std::unique_ptr<Instrument> createInstrument(size_t preset) override;
  std::vector<std::unique_ptr<Instrument> > createAll() override;
  
protected:
  void openFile();

private:
  int samplerate;
  std::string filename;
  std::shared_ptr<SoundFontFile> sf;
};

#endif
