#ifndef _FILEINSTRUMENT_H_
#define _FILEINSTRUMENT_H_

#include "Instrument.h"

#include <string>
#include <vector>

class FileInstrument : public Instrument {
 public:
  explicit FileInstrument(const std::string & _filename) : filename(_filename) {
    openFile();
  }

  std::shared_ptr<InstrumentVoice> createVoice(int _identifier) const;
  
protected:
  void openFile();

private:
  std::string filename;
  std::shared_ptr<SampleData> samples;
};

#endif
