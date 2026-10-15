#ifndef _LEAFTRACK_H_
#define _LEAFTRACK_H_

#include "Track.h"
#include "../ambisonic/SphericalPosition.h"
#include "SendLevels.h"
#include "../ambisonic/SpatialMode.h"

// Shared surface for every addressable, positioned leaf track type
// (InstrumentTrack/SampleTrack/PercussionTrack) - solo/mute/position/sends,
// note-column visibility, and the color/Mute-Solo eligibility every one of
// them gets in the pattern editor (SongStructure::visit()'s
// `dynamic_cast<const LeafTrack *>` check). What's deliberately *not* here:
// `instrument_id_` (InstrumentTrack-only - a pool index only a plain
// pitched instrument track actually resolves this way; see
// PercussionTrack.h for how the other one sources its sound instead).
class LeafTrack : public Track {
 public:
  // Whether live input played into this track is heard - see
  // Controller::isMonitoring().
  enum class Monitor { AUTO, IN, OFF };

  LeafTrack(TrackType type) : Track(type) { }

  void loadParameters(const ParameterSource & input);
  void storeParameters(ParameterSource & output) const override;

  void setElevation(float e) { elevation_ = e; }
  void setAzimuth(float a) { azimuth_ = a; }
  void setDistance(float d) { distance_ = d; }
  void setExtent(float e) { extent_ = e; }

  float getElevation() const { return elevation_; }
  float getAzimuth() const { return azimuth_; }
  float getDistance() const { return distance_; }

  // -1 (the default) means "not authored - resolve to the assigned
  // instrument's own family default instead" (Track::getDefaultExtent());
  // any value >= 0 is an explicit override. See SphericalPosition::extent.
  float getExtent() const { return extent_; }

  // How the notes are placed around the position; AUTO lets the instrument decide.
  SpatialMode getSpatialMode() const { return spatial_mode_; }
  void setSpatialMode(SpatialMode mode) { spatial_mode_ = mode; }

  SphericalPosition getPosition() const { return { azimuth_, elevation_, distance_, extent_ }; }

  bool showNoteColumn() const { return show_note_column_; }
  bool showVelocityColumn() const { return show_velocity_column_; }
  // The track-wide effect command column (one per track).
  bool showTrackFxColumn() const { return show_track_fx_column_; }
  bool showDelayColumn() const { return show_delay_column_; }
  // The local fx column of each note column (Note::getFx()).
  bool showLocalFxColumn() const { return show_local_fx_column_; }

  // A floor VisibleTrackInfo::num_subtracks_ (chord/polyphony note-column
  // width, derived elsewhere from actual note data - see Pattern::
  // getTrackInformation()) is taken the max against, so an "add note
  // column" command can make an empty column appear ahead of typing into
  // it. Never below 1 - that's what showNoteColumn()=false is
  // for (a different concept: hiding note columns entirely).
  int getMinNoteColumns() const { return min_note_columns_; }
  void setMinNoteColumns(int n) { min_note_columns_ = n < 1 ? 1 : n; }

  bool isSolo() const { return solo_; }
  void setSolo(bool s) { solo_ = s; }

  bool isMuted() const { return muted_; }
  void setMuted(bool m) { muted_ = m; }

  Monitor getMonitor() const { return monitor_; }
  void setMonitor(Monitor m) { monitor_ = m; }


private:
  bool solo_ = false, muted_ = false;
  Monitor monitor_ = Monitor::AUTO;
  float elevation_ = 0, azimuth_ = 0, distance_ = 0;
  float extent_ = -1.0f;
  SpatialMode spatial_mode_ = SpatialMode::AUTO;

  bool show_note_column_ = true;
  bool show_velocity_column_ = true;
  bool show_delay_column_ = true;
  bool show_track_fx_column_ = true;
  bool show_local_fx_column_ = true;

  int min_note_columns_ = 1;
};

#endif
