#ifndef _TRACK_H_
#define _TRACK_H_

#include "Note.h"
#include "InstrumentState.h"

#include <deque>
#include <vector>

class Track {
 public:
  explicit Track() {

  }

  float getPan() const { return pan; }
  void setPan(float _pan) { pan = _pan; }

  float getVolume() const { return volume; }
  void setVolume(float _volume) { volume = _volume; }

  bool getSolo() const { return solo; }
  void setSolo(bool s) {solo = s; }

  void setInstrumentId(int id) { instrument_id = id; }
  int getInstrumentId() const { return instrument_id; }

  InstrumentState & getState() { return state; }
  
  void addPendingNotes(size_t frame, const std::vector<Note> & notes) {
    pending_notes.push_back(std::pair(frame, notes));
  }
  void clearPendingNotes() { pending_notes.clear(); }
  std::deque<std::pair<unsigned int, std::vector<Note> > > & getPendingNotes() { return pending_notes; }

  
private:
  int instrument_id = 0;
  InstrumentState state;
  bool solo = false;
  std::deque<std::pair<unsigned int, std::vector<Note> > > pending_notes;
  float pan = 0.5f;
  float volume = 1.0f;  
};

#endif
