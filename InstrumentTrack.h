#ifndef _INSTRUMENTTRACK_H_
#define _INSTRUMENTTRACK_H_

#include "Track.h"

class InstrumentTrack : public Track {
 public:
  InstrumentTrack() : Track(TrackType::INSTRUMENT_CONTROL), instrument_id_(0) { }
  InstrumentTrack(int instrument_id) : Track(TrackType::INSTRUMENT_CONTROL), instrument_id_(instrument_id) { }
  InstrumentTrack(TrackType type) : Track(type), instrument_id_(0) { }

  const char * getElementName() const override { return "track"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override;

  SampleData render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) const override;
  
  void loadParameters(const ParameterSource & input);
  void storeParameters(ParameterSource & output) const override;

  int getInstrumentId() const { return instrument_id_; }
  void setInstrumentId(int id) { instrument_id_ = id; }  

  void setElevation(float e) { elevation_ = e; }
  void setAzimuth(float a) { azimuth_ = a; }
  void setDistance(float d) { distance_ = d; }
  
  float getElevation() const { return elevation_; }
  float getAzimuth() const { return azimuth_; }
  float getDistance() const { return distance_; }

  void setColor(std::string color) { color_ = std::move(color); }
  const std::string & getColor() const { return color_; }

  bool showNoteColumn() const { return show_note_column_; }
  bool showVelocityColumn() const { return show_velocity_column_; }
  bool showEffectsColumn() const { return show_effects_column_; }
  bool showDelayColumn() const { return show_delay_column_; }

private:
  int instrument_id_ = 0;
  float elevation_ = 0, azimuth_ = 0, distance_ = 0;
  std::string color_;
  float portamento_ = -1.0f;

  bool show_note_column_ = true;
  bool show_velocity_column_ = true;
  bool show_delay_column_ = true;
  bool show_effects_column_ = true;
};

#endif

