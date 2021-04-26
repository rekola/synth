#ifndef _TRACK_H_
#define _TRACK_H_

#include "Note.h"
#include "Instrument.h"
#include "InstrumentState.h"

#include <deque>
#include <vector>
#include <memory>

class Track {
 public:
  Track() { }

  float getPan() const { return pan; }
  void setPan(float _pan) { pan = _pan; }

  float getVolume() const { return volume; }
  void setVolume(float _volume) { volume = _volume; }

  bool getSolo() const { return solo; }
  void setSolo(bool s) {solo = s; }

  void setInstrumentId(int id) { instrument_id = id; }
  int getInstrumentId() const { return instrument_id; }

  std::vector<InstrumentState> & getStates() { return states; }
  
  void addPendingNotes(size_t frame, const std::vector<Note> & notes) {
    pending_notes.push_back(std::pair(frame, notes));
  }
  void clearPendingNotes() { pending_notes.clear(); }
  std::deque<std::pair<unsigned int, std::vector<Note> > > & getPendingNotes() { return pending_notes; }

  void playNote(const Note & note, const Instrument & instrument) {
    for (auto & state : states) {
      if (!state.isPlaying()) {
	state.playNote(note, instrument.getTranspose(), instrument.getDetune());
	return;
      }
    }
    states.push_back(instrument.createVoice());
    states.back().playNote(note, instrument.getTranspose(), instrument.getDetune());
  }

private:
  int instrument_id = 0;
  std::vector<InstrumentState> states;
  bool solo = false;
  float pan = 0.5f;
  float volume = 0.15f;
  std::shared_ptr<Track> first_child, next_sibling;

  std::deque<std::pair<unsigned int, std::vector<Note> > > pending_notes;
};

#endif
