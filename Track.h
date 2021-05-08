#ifndef _TRACK_H_
#define _TRACK_H_

#include "TreeElement.h"
#include "Note.h"
#include "Instrument.h"
#include "SampleData.h"
#include "Effect.h"
#include "SampleData.h"
#include "TrackEvent.h"

#include <vector>
#include <memory>
#include <map>

class TrackState;

class Track : public TreeElement {
 public:
  enum Type { GROUP = 1, SEQUENCER, SAMPLE, SUBSONG };

  Track(Type _type = SEQUENCER) : type(_type) { }

  Type getType() { return type; }
  
  float getVolume() const { return volume; }
  void setVolume(float _volume) { volume = _volume; }

  bool isSolo() const { return solo; }
  void setSolo(bool s) { solo = s; }

  bool isMuted() const { return mute; }
  void setMute(bool m) { mute = m; }

  int getInstrumentId() const { return instrument_id; }
  void setInstrumentId(int id) { instrument_id = id; }
  
  float getDetune() const { return detune; }
  void setDetune(float _detune) { detune = _detune; }
  
  SampleData render(size_t frames, TrackState & state, Instrument & instrument, std::map<unsigned int, std::vector<TrackEvent> > & pending_events);
  
  void setSample(std::shared_ptr<SampleData> _sample) { sample = _sample; }

  const std::string & getName() const { return name; }

  std::vector<Track> & getChildren() { return children; }
  const std::vector<Track> & getChildren() const { return children; }

  const Track & getChild(size_t i) const { return children[i]; }
  Track & getChild(size_t i) { return children[i]; }
  Track & addChild(const Track & s) { children.push_back(s); return children.back(); }
  Track & addChild(Track::Type type = Track::SEQUENCER) { return addChild(Track(type)); }

  void addEffect(std::unique_ptr<Effect> effect) { effects.push_back(std::move(effect)); }
  const std::vector<std::shared_ptr<Effect> > & getEffects() const { return effects; }

  void setElevation(float e) { elevation = e; }
  void setAzimuth(float a) { azimuth = a; }
  void setDistance(float d) { distance = d; }

  float getElevation() const { return elevation; }
  float getAzimuth() const { return azimuth; }
  float getDistance() const { return distance; }  
    
private:
  Type type;
  int instrument_id = 0;
  bool solo = false, mute = false;
  float volume = 1.00f;
  float detune = 0;
  std::string name;
  std::vector<Track> children;
  std::shared_ptr<SampleData> sample;
  float elevation = 0, azimuth = 0, distance = 0;

  std::vector<std::shared_ptr<Effect> > effects;
};

#endif
