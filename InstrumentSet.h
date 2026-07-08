#ifndef _INSTRUMENTSET_H_
#define _INSTRUMENTSET_H_

#include "Instrument.h"

#include <vector>
#include <memory>

class InstrumentSet {
 public:
  InstrumentSet() {

  }
  virtual ~InstrumentSet() { }
  
  virtual std::unique_ptr<Instrument> createInstrument(size_t preset, const char * name = 0) = 0;
  virtual std::vector<std::unique_ptr<Instrument> > createAll() = 0;  
};

#endif
