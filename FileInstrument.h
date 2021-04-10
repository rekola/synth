#ifndef _FILEINSTRUMENT_H_
#define _FILEINSTRUMENT_H_

#include "Instrument.h"
#include "WaveformType.h"

#include <string>
#include <vector>

class FileInstrument : public Instrument {
 public:
  explicit FileInstrument(const std::string & _filename) : filename(_filename) {
    openFile();
  }
  
  float getSample(float fphase) const override {
    size_t i = (size_t)fphase;
    if (i >= 0 && i < samples.size()) {
      return samples[i];
    } else {
      return 0;
    }
  }

protected:
  void openFile();

private:
  std::string filename;
  std::vector<float> samples;
};

#endif
