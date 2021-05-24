#ifndef _INSTRUMENTTRACK_H_
#define _INSTRUMENTTRACK_H_

#include "Track.h"

class InstrumentTrack : public Track {
 public:
  InstrumentTrack() : Track(-1, INSTRUMENT), instrument_id(0), detune(0.0f) { }
  InstrumentTrack(int _id, int _instrument_id, float _detune) : Track(_id, INSTRUMENT), instrument_id(_instrument_id), detune(_detune) { }
  
  int getInstrumentId() const { return instrument_id; }
  void setInstrumentId(int id) { instrument_id = id; }
  
  float getDetune() const { return detune; }
  void setDetune(float _detune) { detune = _detune; }
  
  SampleData render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Instrument> > & instruments, TrackEventQueue & events) override;
  
  void readXML(tinyxml2::XMLElement & element);
  void populateXML(tinyxml2::XMLElement & xml_element) const override;
      
private:
  int instrument_id = 0;
  float detune = 0;
};

#endif
