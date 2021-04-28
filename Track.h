#ifndef _TRACK_H_
#define _TRACK_H_

#include "Note.h"
#include "Instrument.h"
#include "InstrumentVoice.h"

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

  int getInstrumentId() const { return instrument_id; }
  void setInstrumentId(int id) {
    if (id != instrument_id) {
      instrument_id = id;
      voices.clear(); // the voices have wrong instrument
    }
  }

  std::vector<std::shared_ptr<InstrumentVoice> > & getVoices() { return voices; }
  
  void addPendingNotes(size_t frame, Tuning tuning, const std::vector<Note> & notes) {
    pending_notes.push_back(std::tuple(frame, tuning, notes));
  }
  void clearPendingNotes() { pending_notes.clear(); }
  std::deque<std::tuple<unsigned int, Tuning, std::vector<Note> > > & getPendingNotes() { return pending_notes; }

  void playNote(Tuning tuning, const Note & note, const Instrument & instrument, int identifier) {
    bool voice_found = false;
    for (auto & voice : voices) {
      if (!voice_found && !voice->isPlaying()) {
	voice->playNote(tuning, note, instrument.getTranspose(), instrument.getDetune());
	voice_found = true;
      } else if (identifier == voice->getIdentifier() && voice->isPlaying()) {
	voice->stopNote();
      }
    }
    if (!voice_found) {
      voices.push_back(instrument.createVoice(identifier));
      voices.back()->playNote(tuning, note, instrument.getTranspose(), instrument.getDetune());
    }
  }

  size_t getVoiceCount() const {
    size_t n = 0;
    for (auto & voice : voices) if (voice->isPlaying()) n++;
    return n;
  }

  size_t getAllocatedVoiceCount() const { return voices.size(); }
  
private:
  int instrument_id = 0;
  std::vector<std::shared_ptr<InstrumentVoice> > voices;
  bool solo = false;
  float pan = 0.5f;
  float volume = 0.75f;
  std::shared_ptr<Track> first_child, next_sibling;

  std::deque<std::tuple<unsigned int, Tuning, std::vector<Note> > > pending_notes;
};

#endif
