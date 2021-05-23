#ifndef _INSTRUMENTTRACK_H_
#define _INSTRUMENTTRACK_H_

#include "Track.h"

class InstrumentTrack : public Track {
 public:
  InstrumentTrack(int _instrument_id = 0, float _detune = 0.0f) : Track(INSTRUMENT), instrument_id(_instrument_id), detune(_detune) { }
  
  int getInstrumentId() const { return instrument_id; }
  void setInstrumentId(int id) { instrument_id = id; }
  
  float getDetune() const { return detune; }
  void setDetune(float _detune) { detune = _detune; }
  
  SampleData render(size_t frames, TrackState & state, const std::vector<std::unique_ptr<Instrument> > & instruments, std::map<unsigned int, std::vector<TrackEvent> > & pending_events) override;
  void populateXML(tinyxml2::XMLElement & xml_element) const override;
      
private:
  int instrument_id = 0;
  float detune = 0;
};

#endif
