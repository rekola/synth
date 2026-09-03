#include "SongStructure.h"
#include "Track.h"
#include "LeafTrack.h"
#include "DrumMachineTrack.h"
#include "Song.h"

SongStructure::SongStructure(const Song & song) {
  visit(song.getMasterTrack());
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
  } else if (track.getType() == TrackType::DRUM_MACHINE) {
    // Step-sequencer compact layout: one narrow NOTE-only cell per lane
    // (DrumMachineTrack::getLaneNotes() order, same as the Launchpad step
    // grid), never velocity/delay - a fixed, known set of up to
    // DrumMachineTrack::kMaxLanes voices needs a column per lane, not a
    // full NOTE/VEL/DEL triplet per lane the way an open-ended chord
    // would (PatternEditor::renderRow()'s own DRUM_MACHINE branch reads
    // this same shape to render/edit each cell). The per-row effect/
    // command column still stays - a DrumMachineTrack's Pattern carries
    // per-row Command data exactly like any other track's.
    VisibleTrackInfo info;
    auto & drum_track = dynamic_cast<const DrumMachineTrack &>(track);
    info.has_note_column_ = true;
    info.num_velocity_columns_ = 0;
    info.has_delay_column_ = false;
    info.has_effect_column_ = drum_track.showEffectsColumn();
    info.updateNumSubtracks(static_cast<int>(drum_track.getLaneNotes().size()));
    info.collapsed_ = drum_track.isCollapsed();
    assign(std::move(info));
  } else if (track.getType() == TrackType::SAMPLE) {
    // Waveform placeholder column plus (SampleTrack is a LeafTrack, same
    // toggle every other leaf type already has) an ordinary command
    // column right after it - see fill_track_info()'s own comment on why
    // an explicit entry is kept rather than left absent. The placeholder
    // itself is much wider than an ordinary NOTE column
    // (VisibleTrackInfo::sample_placeholder_width_) - this is the only
    // content a SampleTrack's row ever shows, and PatternEditor's own
    // waveform-box rendering needs real horizontal room to draw a legible
    // shape, not just enough for a note name.
    auto & sample_track = dynamic_cast<const LeafTrack &>(track);
    VisibleTrackInfo info;
    info.collapsed_ = track.isCollapsed();
    info.sample_placeholder_width_ = 24;
    info.has_effect_column_ = sample_track.showEffectsColumn();
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
  } else if (track.getType() == TrackType::MASTER) {
    // The tree parent of every top-level track (Song::getMasterTrack()) -
    // same shape and order as the EFFECT branch just above (children
    // visited, and given their own columns, before this one assigns its
    // own), so its single effect-command column ends up rightmost, not
    // one more sibling among the tracks it sums. Not a LeafTrack, so
    // assign()'s own color_ordinal_ check leaves it uncolored - which is
    // also what keeps it out of ArrangementGrid (its own
    // color_ordinal_ >= 0 filter) and out of the pattern editor's
    // colored/Mute-Solo heading path, with no further changes needed
    // anywhere else.
    for (auto & child : track.getChildren()) visit(*child);
    VisibleTrackInfo info;
    info.has_note_column_ = false;
    info.has_effect_column_ = true;
    info.collapsed_ = track.isCollapsed();
    info.collapsed_content_width_ = 0;
    assign(std::move(info));
  } else {
    // GROUP and anything else unrecognized - a pure pass-through, no
    // ordinal of its own (see the design plan's own note on why this is
    // this plan's default, not a considered decision).
    for (auto & child : track.getChildren()) visit(*child);
  }
}
