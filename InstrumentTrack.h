#ifndef _INSTRUMENTTRACK_H_
#define _INSTRUMENTTRACK_H_

#include "Track.h"

class InstrumentTrack : public Track {
 public:
  InstrumentTrack() : Track(-1, TrackType::INSTRUMENT_CONTROL), instrument_id(0) { }
  InstrumentTrack(int _id, int _instrument_id) : Track(_id, TrackType::INSTRUMENT_CONTROL), instrument_id(_instrument_id) { }

  std::string getElementName() const override { return "track"; }

  int getInstrumentId() const { return instrument_id; }
  void setInstrumentId(int id) { instrument_id = id; }
    
  SampleData render(int frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events) override;
  
  void loadParameters(const ParameterSource & input);
  void storeParameters(ParameterSource & output) const override;

  void setElevation(float e) { elevation = e; }
  void setAzimuth(float a) { azimuth = a; }
  void setDistance(float d) { distance = d; }
  
  float getElevation() const { return elevation; }
  float getAzimuth() const { return azimuth; }
  float getDistance() const { return distance; }

  bool showNoteColumn() const { return show_note_column; }
  bool showVelocityColumn() const { return show_velocity_column; }
  bool showEffectsColumn() const { return show_effects_column; }
  bool showDelayColumn() const { return show_delay_column; }

private:
  int instrument_id = 0;
  float elevation = 0, azimuth = 0, distance = 0;

  bool show_note_column = true;
  bool show_velocity_column = true;
  bool show_delay_column = true;
  bool show_effects_column = true;
};

#endif
