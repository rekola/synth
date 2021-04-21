#ifndef _SEQUENCE_H_
#define _SEQUENCE_H_

#include <vector>

#define PATTLEN 32

class Sequence {
 public:
  explicit Sequence() {
    for (size_t i = 0; i < PATTLEN; i++) addNote(0);
  }

  void setNote(size_t i, unsigned char note) {
    while ( i >= size() ) addNote(0);
    notes[i] = note;
  }

  unsigned char getNote(size_t i) const { return i < notes.size() ? notes[i] : 0; }
  size_t size() const { return notes.size(); }

  void setInstrumentId(int id) { instrument_id = id; }
  int getInstrumentId() const { return instrument_id; }
  
private:
  void addNote(unsigned char n) { notes.push_back(n); }

  int instrument_id = 0;
  std::vector<unsigned char> notes;
};

#endif
