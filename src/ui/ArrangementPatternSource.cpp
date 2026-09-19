#include "ArrangementPatternSource.h"
#include "../Controller.h"
#include "../model/Song.h"
#include "../model/Section.h"
#include "../model/Clip.h"
#include "../model/PatternGrid.h"

Song &
ArrangementPatternSource::song() const {
  return controller_.getSong();
}

RowAddress
ArrangementPatternSource::cursor() const {
  auto & info = controller_.getPlaybackInfo();
  return { info.getPatternIndex(), info.getRowIndex() };
}

void
ArrangementPatternSource::moveCursor(int delta_rows) {
  controller_.moveEditPosition(delta_rows);
}

RowAddress
ArrangementPatternSource::normalize(int block, int row) const {
  auto [ section_idx, section_row ] = song().normalizePosition(block, row);
  return { section_idx, section_row };
}

int
ArrangementPatternSource::blockCount() const {
  return static_cast<int>(song().getSections().size());
}

int
ArrangementPatternSource::blockLength(int block) const {
  return song().getEffectiveSectionLength(block);
}

ReadTarget
ArrangementPatternSource::read(int track_id, RowAddress address) const {
  auto & s = song();
  return resolveReadTarget(s, s.getSection(address.block), track_id, address.row, controller_.getFocusedClip());
}

EditTarget
ArrangementPatternSource::edit(int track_id, RowAddress address) {
  auto & s = song();
  // Writes - see Song::getOrCreateSection()'s own comment.
  return resolveEditTarget(s, s.getOrCreateSection(address.block), track_id, address.row, controller_.getFocusedClip());
}

std::unique_ptr<const PatternGrid>
ArrangementPatternSource::readGrid(RowAddress anchor) const {
  const Song & s = song();
  return std::make_unique<SectionRegionGrid>(s, s.getSection(anchor.block), anchor.row, controller_.getFocusedClip());
}

std::unique_ptr<PatternGrid>
ArrangementPatternSource::editGrid(RowAddress anchor, bool create) {
  auto & s = song();
  auto & section = create ? s.getOrCreateSection(anchor.block) : s.getSection(anchor.block);
  return std::make_unique<SectionRegionGrid>(s, section, anchor.row, controller_.getFocusedClip());
}

std::pair<int, int>
ArrangementPatternSource::sourceRows(int track_id, RowAddress anchor) const {
  const Song & s = song();
  return SectionRegionGrid(s, s.getSection(anchor.block), anchor.row, controller_.getFocusedClip()).sourceRows(track_id);
}

void
ArrangementPatternSource::collectTrackInfo(RowAddress first, int rows, std::unordered_map<int, VisibleTrackInfo> & track_info) const {
  const Song & s = song();
  for (auto row = 0; row < rows; ) {
    auto [ section_idx, section_row ] = s.normalizePosition(first.block, row + first.row);
    if (section_idx >= static_cast<int>(s.getSections().size())) break;

    auto & section = s.getSection(section_idx);
    section.getTrackInformation(track_info);

    // section.getTrackInformation() above only ever scans each track's own
    // background Pattern - a placed clip instance's own leaf Pattern lives
    // entirely outside patterns_by_track_id_, so a chord recorded (or
    // otherwise authored) into one is invisible to it, and the track would
    // show too few note columns to display it. Every clip actually placed
    // somewhere in this section gets the same treatment here instead -
    // covers a note-recording session's own newly-placed clip
    // (Controller::ensureNoteRecordingClip()) the same way it covers any
    // other clip, rather than special-casing recording specifically.
    for (auto & [ instance_track_id, instances ] : section.getInstancesByTrack()) {
      auto & clips = s.getClips(instance_track_id);
      for (auto & [ instance_row, clip_id ] : instances) {
        if (clip_id == "OFF") continue;
        for (auto & clip : clips) {
          if (clip.getId() != clip_id) continue;
          // A SampleTrack's own clip carries raw audio, not a Pattern -
          // its getLeafPattern() is just an unused, empty Pattern, so
          // reading subtrack info from it would be meaningless
          // (ArrangementOps.cpp's own resolveReadTarget()/
          // resolveEditTarget() guard against the same thing).
          if (!clip.hasSample()) clip.getLeafPattern().updateSubtrackInfo(track_info[instance_track_id]);
          break;
        }
      }
    }

    row += s.getEffectiveSectionLength(section) - section_row;
  }
}

const Section *
ArrangementPatternSource::annotations(int block) const {
  const Song & s = song();
  return &s.getSection(block);
}

Section *
ArrangementPatternSource::annotations(int block, bool create) {
  auto & s = song();
  return create ? &s.getOrCreateSection(block) : &s.getSection(block);
}

void
ArrangementPatternSource::insertRow(int track_id, RowAddress address) {
  auto & s = song();
  // insert-row writes - see Song::getOrCreateSection()'s own comment.
  auto & section = s.getOrCreateSection(address.block);
  section.insertRowForTrack(track_id, address.row, s.getEffectiveSectionLength(section));
}

bool
ArrangementPatternSource::hasInstance(int track_id, RowAddress address) const {
  const Song & s = song();
  return resolveInstanceAt(s, s.getSection(address.block), track_id, address.row).clip_index >= 0;
}

bool
ArrangementPatternSource::stopInstance(int track_id, RowAddress address) {
  auto & s = song();
  auto & section = s.getSection(address.block);
  if (resolveInstanceAt(s, section, track_id, address.row).clip_index == Section::kNoInstance) return false;
  placeStopInstance(section, track_id, address.row);
  return true;
}

const SampleContent *
ArrangementPatternSource::sampleBackground(int track_id, int block) const {
  const Song & s = song();
  return s.getSection(block).getSampleBackgroundContent(track_id);
}
