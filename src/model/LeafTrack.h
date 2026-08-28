#ifndef _LEAFTRACK_H_
#define _LEAFTRACK_H_

#include "Track.h"
#include "../ambisonic/SphericalPosition.h"
#include "SendLevels.h"

// Shared surface for every addressable, positioned leaf track type
// (InstrumentTrack/SampleTrack/PercussionTrack/DrumMachineTrack) - solo/
// mute/position/sends, note-column visibility, and the color/Mute-Solo
// eligibility every one of them gets in the pattern editor (SongStructure::
// visit()'s `dynamic_cast<const LeafTrack *>` check). What's deliberately
// *not* here: `instrument_id_` (InstrumentTrack-only - a pool index only a
// plain pitched instrument track actually resolves this way; see
// PercussionTrack.h/DrumMachineTrack.h for how the other two source their
// sound instead).
class LeafTrack : public Track {
 public:
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

  SphericalPosition getPosition() const { return { azimuth_, elevation_, distance_, extent_ }; }

  bool showNoteColumn() const { return show_note_column_; }
  bool showVelocityColumn() const { return show_velocity_column_; }
  bool showEffectsColumn() const { return show_effects_column_; }
  bool showDelayColumn() const { return show_delay_column_; }

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

  // Plain linear multipliers, same as SendLevels.h's own fields (see its
  // doc comment) - dB is only ever a control-surface/file-format unit, one
  // layer up from here (Controller::setTrackSendA()/setTrackSendB()/
  // setTrackSendMain(), loadParameters()/storeParameters() below).
  const SendLevels & getSends() const { return sends_; }
  void setSendA(float s) { sends_.a = s; }
  void setSendB(float s) { sends_.b = s; }
  void setSendMain(float s) { sends_.main = s; }

private:
  bool solo_ = false, muted_ = false;
  float elevation_ = 0, azimuth_ = 0, distance_ = 0;
  float extent_ = -1.0f;
  SendLevels sends_;

  bool show_note_column_ = true;
  bool show_velocity_column_ = true;
  bool show_delay_column_ = true;
  bool show_effects_column_ = true;

  int min_note_columns_ = 1;
};

#endif
