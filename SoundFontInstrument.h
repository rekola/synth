#ifndef _SOUNDFONTINSTRUMENT_H_
#define _SOUNDFONTINSTRUMENT_H_

#include "Instrument.h"

#include <string>

typedef struct tsf tsf;

class SoundFontInstrument : public Instrument {
 public:  
  explicit SoundFontInstrument(std::string _filename) : filename(_filename) {
    openFile();
  }
  ~SoundFontInstrument();

  float getSample(InstrumentVoice & voice) const override;
  void openFile();
  std::shared_ptr<InstrumentVoice> createVoice() const override;
  
private:
  tsf * tsf_handle = 0;
  std::string filename;
};

#endif
