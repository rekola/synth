#ifndef _TRACK_H_
#define _TRACK_H_

#include "Note.h"
#include "Instrument.h"
#include "InstrumentVoice.h"
#include "SampleData.h"
#include "Effect.h"

#include <map>
#include <vector>
#include <memory>

class Track {
 public:
  enum Type { MASTER = 1, GROUP, SEQUENCER, SAMPLE };

  Track(Type _type = SEQUENCER) : type(_type) { }

  Type getType() { return type; }
  
  float getPan() const { return pan; }
  void setPan(float _pan) { pan = _pan; }

  float getVolume() const { return volume; }
  void setVolume(float _volume) { volume = _volume; }

  bool isSolo() const { return solo; }
  void setSolo(bool s) { solo = s; }

  bool isMuted() const { return mute; }
  void setMute(bool m) { mute = m; }

  int getInstrumentId() const { return instrument_id; }
  void setInstrumentId(int id) {
    if (id != instrument_id) {
      instrument_id = id;
      voices.clear(); // the voices have wrong instrument
    }
  }

  std::vector<std::shared_ptr<InstrumentVoice> > & getVoices() { return voices; }
  
  void addPendingNote(size_t frame, short id, Tuning tuning, const Note & note) {
    pending_notes[frame].push_back(std::tuple(id, tuning, note));
  }
  void clearPendingNotes() { pending_notes.clear(); }
  std::map<unsigned int, std::vector<std::tuple<int, Tuning, Note> > > & getPendingNotes() { return pending_notes; }

  void playNote(Tuning tuning, const Note & note, const Instrument & instrument, int identifier) {
    bool voice_found = false;
    for (auto & voice : voices) {
      if (!voice_found && !voice->isPlaying()) {
	voice->setIdentifier(identifier);
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
    for (auto & child : children) n += child.getVoiceCount();
    return n;
  }

  size_t getAllocatedVoiceCount() const {
    size_t n = voices.size();
    for (auto & child : children) n += child.getAllocatedVoiceCount();
    return n;
  }

  void setSample(std::shared_ptr<SampleData> _sample) { sample = _sample; }

  const std::string & getName() const { return name; }

  std::vector<Track> & getChildren() { return children; }
  const std::vector<Track> & getChildren() const { return children; }

  const Track & getChild(size_t i) const { return children[i]; }
  Track & getChild(size_t i) { return children[i]; }
  Track & addChild(const Track & s) { children.push_back(s); return children.back(); }
  Track & addChild(Track::Type type = Track::SEQUENCER) { return addChild(Track(type)); }

  void applyEffects(SampleData & data) {
    for (auto & effect : effects) {
      effect->apply(data);
    }
  }

  void addEffect(std::unique_ptr<Effect> effect) { effects.push_back(std::move(effect)); }
  
private:
  Type type;
  int instrument_id = 0;
  std::vector<std::shared_ptr<InstrumentVoice> > voices;
  bool solo = false, mute = false;
  float pan = 0.5f;
  float volume = 0.75f;
  std::string name;
  std::vector<Track> children;
  std::shared_ptr<SampleData> sample;

  std::vector<std::shared_ptr<Effect> > effects;

  std::map<unsigned int, std::vector<std::tuple<int, Tuning, Note> > > pending_notes;
};

#endif
