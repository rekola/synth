#include "ArrangementPatternSource.h"
#include "../Controller.h"
#include "../model/Song.h"
#include "../model/Arrangement.h"
#include "../model/Clip.h"
#include "../model/PatternGrid.h"

#include <algorithm>

Song &
ArrangementPatternSource::song() const {
  return controller_.getSong();
}

RowAddress
ArrangementPatternSource::cursor() const {
  return { 0, controller_.getPlaybackInfo().getAbsolutePosition() };
}

void
ArrangementPatternSource::moveCursor(int delta_rows) {
  controller_.moveEditPosition(delta_rows);
}

RowAddress
ArrangementPatternSource::normalize(int block, int row) const {
  if (block > 0 || row >= Song::kMaxArrangementRows) return { 1, 0 }; // past the end
  return { 0, row };
}

int
ArrangementPatternSource::blockLength(int) const {
  return Song::kMaxArrangementRows;
}

ReadTarget
ArrangementPatternSource::read(int track_id, RowAddress address) const {
  return resolveReadTarget(song(), track_id, address.row, controller_.getFocusedClip());
}

EditTarget
ArrangementPatternSource::edit(int track_id, RowAddress address) {
  return resolveEditTarget(song(), track_id, address.row, controller_.getFocusedClip());
}

std::unique_ptr<const PatternGrid>
ArrangementPatternSource::readGrid(RowAddress anchor) const {
  const Song & s = song();
  return std::make_unique<ArrangementRegionGrid>(s, anchor.row, controller_.getFocusedClip());
}

std::unique_ptr<PatternGrid>
ArrangementPatternSource::editGrid(RowAddress anchor, bool) {
  return std::make_unique<ArrangementRegionGrid>(song(), anchor.row, controller_.getFocusedClip());
}

std::pair<int, int>
ArrangementPatternSource::sourceRows(int track_id, RowAddress anchor) const {
  const Song & s = song();
  return ArrangementRegionGrid(s, anchor.row, controller_.getFocusedClip()).sourceRows(track_id);
}

void
ArrangementPatternSource::collectTrackInfo(RowAddress, int, std::unordered_map<int, VisibleTrackInfo> & track_info) const {
  const Song & s = song();
  auto & arrangement = s.getArrangement();
  arrangement.getTrackInformation(track_info);

  // getTrackInformation() above only scans each track's own background
  // Pattern - a placed clip's own leaf Pattern lives outside it, so a
  // chord recorded into one would otherwise show too few note columns.
  // Every clip placed anywhere gets the same treatment, a
  // note-recording session's own newly-placed clip (Controller::
  // ensureNoteRecordingClip()) included.
  for (auto & [ instance_track_id, instances ] : arrangement.getInstancesByTrack()) {
    auto & clips = s.getClips(instance_track_id);
    for (auto & [ instance_row, clip_id ] : instances) {
      if (clip_id == "OFF") continue;
      for (auto & clip : clips) {
        if (clip.getId() != clip_id) continue;
        // A SampleTrack's own clip carries raw audio, not a Pattern.
        if (!clip.hasSample()) clip.getLeafPattern().updateSubtrackInfo(track_info[instance_track_id]);
        break;
      }
    }
  }
}

std::optional<int>
ArrangementPatternSource::locatorRow(RowAddress address) const {
  return address.row;
}

void
ArrangementPatternSource::insertRow(int track_id, RowAddress address) {
  auto & s = song();
  // Everything below shifts down, up to the content's end.
  auto end = std::max(s.getArrangementLength(), address.row + 1) + 1;
  s.getArrangement().insertRowForTrack(track_id, address.row, std::min(end, Song::kMaxArrangementRows));
}

bool
ArrangementPatternSource::hasInstance(int track_id, RowAddress address) const {
  return resolveInstanceAt(song(), track_id, address.row).clip_index >= 0;
}

bool
ArrangementPatternSource::stopInstance(int track_id, RowAddress address) {
  auto & s = song();
  if (resolveInstanceAt(s, track_id, address.row).clip_index == Arrangement::kNoInstance) return false;
  placeStopInstance(s, track_id, address.row);
  return true;
}

const SampleContent *
ArrangementPatternSource::sampleBackground(int track_id, int) const {
  return song().getArrangement().getSampleBackgroundContent(track_id);
}
