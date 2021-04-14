#ifndef _SEQUENCE_H_
#define _SEQUENCE_H_

#include <vector>

class Sequence {
 public:
  explicit Sequence() { }

  void setNote(size_t i, unsigned char note) {
    while ( i >= size() ) addNote(0);
    notes[i] = note;
  }

  unsigned char getNote(size_t i) const { return i < notes.size() ? notes[i] : 0; }
  void addNote(unsigned char n) { notes.push_back(n); }
  size_t size() const { return notes.size(); }

  void setInstrumentId(int id) { instrument_id = id; }
  int getInstrumentId() const { return instrument_id; }
  
private:
  int instrument_id = 1;  
  std::vector<unsigned char> notes;
};

#endif
