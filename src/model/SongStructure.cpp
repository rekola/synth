#include "SongStructure.h"
#include "Track.h"
#include "LeafTrack.h"
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
    // Whether `track` gets a color at all is decided right here, once,
    // for every branch below that calls assign() - a LeafTrack (every
    // addressable leaf track type - InstrumentControl/PercussionControl/
    // DrumMachine/Sample/Arpeggiator - see LeafTrack.h) gets the next
    // sequential slot; anything else (Effect, Group) stays at
    // VisibleTrackInfo::color_ordinal_'s own default of -1. See that
    // field's own comment for why this must be its own counter, not
    // ordinal_by_id_/ordered_ids_.size() below.
    if (dynamic_cast<const LeafTrack *>(&track)) info.color_ordinal_ = next_color_ordinal_++;
    ordinal_by_id_[id] = static_cast<int>(ordered_ids_.size());
    ordered_ids_.push_back(id);
    baseline_info_[id] = std::move(info);
  };

  if (track.getType() == TrackType::INSTRUMENT_CONTROL || track.getType() == TrackType::PERCUSSION_CONTROL) {
    VisibleTrackInfo info;
    auto & leaf_track = dynamic_cast<const LeafTrack &>(track);
    info.has_note_column_ = leaf_track.showNoteColumn();
    info.num_velocity_columns_ = leaf_track.showVelocityColumn() ? 1 : 0;
    info.has_delay_column_ = leaf_track.showDelayColumn();
    info.has_effect_column_ = leaf_track.showEffectsColumn();
    info.updateNumSubtracks(leaf_track.getMinNoteColumns());
    info.collapsed_ = leaf_track.isCollapsed();
    assign(std::move(info));
  } else if (track.getType() == TrackType::DRUM_MACHINE || track.getType() == TrackType::SAMPLE) {
    // Single placeholder column - see fill_track_info()'s own comment on
    // why an explicit, default-constructed entry is kept rather than left
    // absent.
    VisibleTrackInfo info;
    info.collapsed_ = track.isCollapsed();
    assign(std::move(info));
  } else if (track.getType() == TrackType::EFFECT) {
    // Every per-track effect (Chorus/Compressor/TapeDegradation/...) gets
    // its own ordinal and single effect-command column, wrapped or not -
    // see the design plan's "What qualifies for an ordinal". Recurses into
    // its children too, unlike the two leaf cases above: a wrapped
    // instrument underneath still needs its own ordinal as well. Children
    // are visited (and get their columns placed) before the effect track
    // assigns its own, so the effect-command column sits to the right of
    // whatever it's wrapping rather than in front of it.
    for (auto & child : track.getChildren()) visit(*child);
    VisibleTrackInfo info;
    info.has_note_column_ = false;
    info.has_effect_column_ = true;
    // A real per-track user toggle (see Track::isCollapsed()) - effect
    // tracks just default to collapsed there, since there's rarely any
    // per-track command content worth showing at full width until the
    // artist actually starts using one.
    info.collapsed_ = track.isCollapsed();
    // No heading toggle of its own while collapsed (see
    // VisibleTrackInfo::collapsed_content_width_'s own comment) - its
    // ancestor-row box already carries one.
    info.collapsed_content_width_ = 0;
    assign(std::move(info));
  } else {
    // GROUP and anything else unrecognized - a pure pass-through, no
    // ordinal of its own (see the design plan's own note on why this is
    // this plan's default, not a considered decision).
    for (auto & child : track.getChildren()) visit(*child);
  }
}
