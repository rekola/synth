#include "SongStructure.h"
#include "Track.h"
#include "InstrumentTrack.h"
#include "Song.h"

SongStructure::SongStructure(const Song & song) {
  for (auto & track : song.getTracks()) visit(*track);
}

int
SongStructure::getOrdinalFor(int internal_id) const {
  auto it = ordinal_by_id_.find(internal_id);
  return it != ordinal_by_id_.end() ? it->second : -1;
}

int
SongStructure::getOrdinalFor(const Track & track) const {
  return getOrdinalFor(track.getInternalId());
}

const VisibleTrackInfo &
SongStructure::getBaselineInfo(int internal_id) const {
  static const VisibleTrackInfo empty;
  auto it = baseline_info_.find(internal_id);
  return it != baseline_info_.end() ? it->second : empty;
}

// Mirrors Song.cpp's collectRootTrackIds()/PatternEditor.cpp's
// fill_track_info() - both fold into this single walk (see the design plan)
// instead of keeping their own near-duplicate copies of "which nodes are the
// real addressable tracks."
void
SongStructure::visit(const Track & track) {
  auto assign = [&](VisibleTrackInfo info) {
    auto id = track.getInternalId();
    ordinal_by_id_[id] = static_cast<int>(ordered_ids_.size());
    ordered_ids_.push_back(id);
    baseline_info_[id] = std::move(info);
  };

  if (track.getType() == TrackType::INSTRUMENT_CONTROL || track.getType() == TrackType::PERCUSSION_CONTROL) {
    VisibleTrackInfo info;
    auto & instrument_track = dynamic_cast<const InstrumentTrack &>(track);
    info.has_note_column_ = instrument_track.showNoteColumn();
    info.num_velocity_columns_ = instrument_track.showVelocityColumn() ? 1 : 0;
    info.has_delay_column_ = instrument_track.showDelayColumn();
    info.has_effect_column_ = instrument_track.showEffectsColumn();
    info.updateNumSubtracks(instrument_track.getMinNoteColumns());
    assign(std::move(info));
  } else if (track.getType() == TrackType::DRUM_MACHINE || track.getType() == TrackType::SAMPLE) {
    // Single placeholder column - see fill_track_info()'s own comment on
    // why an explicit, default-constructed entry is kept rather than left
    // absent.
    assign(VisibleTrackInfo());
  } else if (track.getType() == TrackType::EFFECT) {
    // Every per-track effect (Chorus/Compressor/TapeDegradation/...) gets
    // its own ordinal and single effect-command column, wrapped or not -
    // see the design plan's "What qualifies for an ordinal". Recurses into
    // its children too, unlike the two leaf cases above: a wrapped
    // instrument underneath still needs its own ordinal as well.
    VisibleTrackInfo info;
    info.has_note_column_ = false;
    info.has_effect_column_ = true;
    assign(std::move(info));
    for (auto & child : track.getChildren()) visit(*child);
  } else {
    // GROUP and anything else unrecognized - a pure pass-through, no
    // ordinal of its own (see the design plan's own note on why this is
    // this plan's default, not a considered decision).
    for (auto & child : track.getChildren()) visit(*child);
  }
}
