#ifndef _TRACK_H_
#define _TRACK_H_

#include "Note.h"

#include <vector>

#define PATTLEN 32

class Track {
 public:
  explicit Track() {
    for (size_t i = 0; i < PATTLEN; i++) addNote();
  }

  void setNote(size_t i, Note note) {
    while ( i >= size() ) addNote();
    notes[i] = note;
  }

  const Note & getNote(size_t i) const { return i < notes.size() ? notes[i] : empty_note; }
  size_t size() const { return notes.size(); }

  void setInstrumentId(int id) { instrument_id = id; }
  int getInstrumentId() const { return instrument_id; }
  
private:
  void addNote() { notes.push_back(Note()); }
  void addNote(Note note) { notes.push_back(note); }

  int instrument_id = 0;
  std::vector<Note> notes;
  Note empty_note;
};

#endif
