#ifndef _TRACK_H_
#define _TRACK_H_

#include "Note.h"
#include "InstrumentState.h"

#include <vector>
#include <deque>

class Track {
 public:
  enum Type { NOTES = 1, AUDIO };
  
  explicit Track(Type _type = NOTES) : type(_type) {

  }

  Type getType() const { return type; }

  bool getSolo() const { return solo; }
    void setSolo(bool s) {solo = s; }

  void setNote(size_t i, Note note) {
    while ( i >= size() ) addNote();
    notes[i] = note;
  }

  const Note & getNote(size_t i) const { return i < notes.size() ? notes[i] : empty_note; }
  size_t size() const { return notes.size(); }

  void setInstrumentId(int id) { instrument_id = id; }
  int getInstrumentId() const { return instrument_id; }

  InstrumentState & getState() { return state; }
  
  void addPendingNote(size_t frame, Note note) {
    pending_notes.push_back(std::pair(frame, note));
  }
  void clearPendingNotes() { pending_notes.clear(); }
  std::deque<std::pair<unsigned int, Note> > & getPendingNotes() { return pending_notes; }

private:
  void addNote() { notes.push_back(Note()); }
  void addNote(Note note) { notes.push_back(note); }

  Type type;
  int instrument_id = 0;
  InstrumentState state;
  std::vector<Note> notes;
  bool solo = false;
  std::deque<std::pair<unsigned int, Note> > pending_notes;

  Note empty_note;
};

#endif
