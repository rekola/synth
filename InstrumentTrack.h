#ifndef _INSTRUMENTTRACK_H_
#define _INSTRUMENTTRACK_H_

#include "Track.h"
#include "SphericalPosition.h"
#include "SendLevels.h"

class InstrumentTrack : public Track {
 public:
  InstrumentTrack() : Track(TrackType::INSTRUMENT_CONTROL), instrument_id_(0) { }
  InstrumentTrack(int instrument_id) : Track(TrackType::INSTRUMENT_CONTROL), instrument_id_(instrument_id) { }
  InstrumentTrack(TrackType type) : Track(type), instrument_id_(0) { }

  const char * getElementName() const override { return "track"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override;
  
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

  SphericalPosition getPosition() const { return { azimuth_, elevation_, distance_ }; }

  void setColor(std::string color) { color_ = std::move(color); }
  const std::string & getColor() const { return color_; }

  bool showNoteColumn() const { return show_note_column_; }
  bool showVelocityColumn() const { return show_velocity_column_; }
  bool showEffectsColumn() const { return show_effects_column_; }
  bool showDelayColumn() const { return show_delay_column_; }

  // A floor VisibleTrackInfo::num_subtracks_ (chord/polyphony note-column
  // width, derived elsewhere from actual note data - see Pattern::
  // getTrackInformation()) is taken the max against, so a Renoise-style
  // "add note column" command can make an empty column appear ahead of
  // typing into it. Never below 1 - that's what showNoteColumn()=false is
  // for (a different concept: hiding note columns entirely).
  int getMinNoteColumns() const { return min_note_columns_; }
  void setMinNoteColumns(int n) { min_note_columns_ = n < 1 ? 1 : n; }

  bool isSolo() const { return solo_; }
  void setSolo(bool s) { solo_ = s; }

  bool isMuted() const { return muted_; }
  void setMuted(bool m) { muted_ = m; }

  // See SendLevels.h.
  const SendLevels & getSends() const { return sends_; }
  void setSendA(float s) { sends_.a = s; }
  void setSendB(float s) { sends_.b = s; }
  void setSendMain(float s) { sends_.main = s; }

private:
  int instrument_id_ = 0;
  bool solo_ = false, muted_ = false;
  float elevation_ = 0, azimuth_ = 0, distance_ = 0;
  std::string color_;
  SendLevels sends_;

  bool show_note_column_ = true;
  bool show_velocity_column_ = true;
  bool show_delay_column_ = true;
  bool show_effects_column_ = true;

  int min_note_columns_ = 1;
};

#endif

