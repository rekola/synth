#include "ArrangementOps.h"

#include "Song.h"
#include "Section.h"
#include "Clip.h"
#include "Pattern.h"
#include "SampleTrack.h"
#include "SampleContent.h"
#include "../audio/AudioBuffer.h"
#include "../ambisonic/ChannelConfiguration.h"

#include <algorithm>

using namespace std;

int
quantizedBarRow(int raw_row, int rows_per_bar) {
  rows_per_bar = max(1, rows_per_bar);
  return ((raw_row + rows_per_bar - 1) / rows_per_bar) * rows_per_bar;
}

int
previousBarRow(int raw_row, int rows_per_bar) {
  rows_per_bar = max(1, rows_per_bar);
  return (raw_row / rows_per_bar) * rows_per_bar;
}

void
placeClipInstance(const Song & song, Section & section, int track_id, int row, int clip_index) {
  auto & clips = song.getClips(track_id);
  if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return;
  auto & clip = clips[static_cast<size_t>(clip_index)];
  auto length = clip.getLength() > 0 ? clip.getLength() : 1;
  auto reach_end = clip.isLooping() ? song.getEffectiveSectionLength(section) - 1 : row + length - 1;

  // Collected first, then cleared in a separate pass - clearInstance()
  // mutates the same map getInstancesForTrack() returns a reference
  // into, so erasing while iterating it directly would be unsafe.
  vector<int> rows_to_clear;
  for (auto & [ existing_row, existing_clip_id ] : section.getInstancesForTrack(track_id)) {
    if (existing_row >= row && existing_row <= reach_end) rows_to_clear.push_back(existing_row);
  }
  for (auto r : rows_to_clear) section.clearInstance(track_id, r);

  // Stores the clip's own stable id, not `clip_index` itself - Clip.h's
  // own comment on why.
  section.setInstance(track_id, row, clip.getId());
}

void
placeStopInstance(Section & section, int track_id, int row) {
  section.setInstance(track_id, row, "OFF");
}

bool
mergeClipToBackground(const Song & song, Section & section, int track_id, int row, const ChannelConfiguration & channel_config) {
  auto active = resolveInstanceAt(song, section, track_id, row);
  if (active.clip_index < 0) return false; // nothing real placed here

  auto & clip = song.getClips(track_id)[static_cast<size_t>(active.clip_index)];
  auto section_length = song.getEffectiveSectionLength(section);

  if (clip.hasSample()) {
    // getMixedContent() - layer 0 directly for the overwhelming majority
    // of (never-overdubbed) clips, or the pre-mixed sum of every layer
    // once there's more than one (Clip.h's own comment) - either way,
    // exactly what a listener actually hears from this clip, so it's
    // what gets baked into the background bed too.
    auto & content = clip.getMixedContent();
    auto output_rate = channel_config.getAudioOutSampleRate();
    auto song_tempo = song.getTempo();
    auto resolved = resolveSampleAudio(content, output_rate, song_tempo);
    if (!resolved.samples || resolved.in_frame >= resolved.out_frame) return false; // nothing playable to merge

    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    auto reach_end = clip.isLooping() ? section_length - 1 : min(active.start_row + length - 1, section_length - 1);

    // A section needs a stable id of its own the moment it first gets a
    // real background bed - Song::generateUniqueSectionId()'s own comment
    // has the full reasoning (its sidecar file's own name has to survive
    // this section later being reordered, which an ordinal position can't).
    if (section.getId().empty()) section.setId(song.generateUniqueSectionId());

    auto sample_interval = channel_config.getSampleInterval(song_tempo);
    auto needed_frames = static_cast<int64_t>(section_length) * sample_interval;
    auto & background = section.getOrCreateSampleBackgroundContent(track_id);

    auto src = resolved.samples->getChannelData(0) + resolved.in_frame;
    auto src_frame_count = static_cast<int64_t>(resolved.out_frame - resolved.in_frame);

    // One mix per lap, matching real playback's own per-lap retrigger
    // (SongState.h's row-scheduling loop) - a looping clip's own real
    // audio essentially never divides its placement's row span evenly, so
    // baking a single pass straight through would silently drop the
    // repeats a listener actually would have heard.
    for (auto lap_start_row = active.start_row; lap_start_row <= reach_end; lap_start_row += length) {
      auto lap_end_row = clip.isLooping() ? min(lap_start_row + length - 1, reach_end) : reach_end;
      auto lap_span_frames = static_cast<int64_t>(lap_end_row - lap_start_row + 1) * sample_interval;
      auto frames_to_mix = min(src_frame_count, lap_span_frames);
      auto dest_offset = static_cast<int64_t>(lap_start_row) * sample_interval;
      mixIntoSampleContent(background, output_rate, needed_frames, src, frames_to_mix, dest_offset, 1.0f);
      if (!clip.isLooping()) break;
    }
  } else {
    auto & leaf = clip.getLeafPattern();
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    auto reach_end = clip.isLooping() ? section_length - 1 : min(active.start_row + length - 1, section_length - 1);

    auto & background = section.getPatternsByTrack()[track_id];
    for (auto r = active.start_row; r <= reach_end; r++) {
      auto src_row = leaf.getEffectiveRow(r - active.start_row, length);
      background.setNotes(r, leaf.getNotes(src_row));
      // Every command column, not just column 0 - a clip's own commands
      // can span more than one the same way its notes can (see
      // Pattern::setCommand(row, command_column, Command)'s own comment).
      auto & cv = leaf.getCommandsAt(src_row);
      background.clearCommands(r);
      for (size_t col = 0; col < cv.size(); col++) {
	if (cv[col].isDefined()) background.setCommand(r, static_cast<int>(col), cv[col]);
      }
    }
  }

  placeStopInstance(section, track_id, active.start_row);
  return true;
}

void
deleteClip(Song & song, int track_id, int clip_index) {
  auto & clips = song.getClips(track_id);
  if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return;
  auto clip_id = clips[static_cast<size_t>(clip_index)].getId();

  // Every section, not just the one(s) the caller happens to know about -
  // the same clip can be (and, live-linked editing being the whole point
  // of a clip, often is) placed in several.
  for (size_t i = 0; i < song.getSections().size(); i++) {
    auto & section = song.getSection(static_cast<int>(i));
    // Collected first, then cleared in a separate pass - same reasoning
    // placeClipInstance() above already documents.
    vector<int> rows_to_clear;
    for (auto & [ row, existing_clip_id ] : section.getInstancesForTrack(track_id)) {
      if (existing_clip_id == clip_id) rows_to_clear.push_back(row);
    }
    for (auto row : rows_to_clear) section.clearInstance(track_id, row);
  }

  // Reset in place to a fresh, id-less filler (Song::ensureClipAt()'s own
  // "hole" state), never erased outright - holes are allowed, and every
  // other track's own scene rows are indexed against this same track's
  // clip list, so shifting everything past the deleted one down (the way
  // a plain vector erase would) would silently misalign them all against
  // it, even when the deletion happened on a completely different track.
  clips[static_cast<size_t>(clip_index)] = Clip(track_id);
  song.incVersion();
}

ActiveInstance
resolveInstanceAt(const Song & song, const Section & section, int track_id, int row) {
  auto & track_instances = section.getInstancesForTrack(track_id);
  if (track_instances.empty()) return { Section::kNoInstance };

  auto it = track_instances.upper_bound(static_cast<unsigned short>(row));
  if (it == track_instances.begin()) return { Section::kNoInstance }; // nothing at or before row
  --it;
  auto event_row = static_cast<int>(it->first);
  auto & clip_id = it->second;
  if (clip_id == "OFF") return { Section::kStopInstance, event_row };

  // The stored id's own *current* position in the track's clip list -
  // never assumed to still be wherever it was when the instance was
  // placed (Clip.h's own comment on why).
  auto & clips = song.getClips(track_id);
  int clip_index = -1;
  for (size_t i = 0; i < clips.size(); i++) {
    if (clips[i].getId() == clip_id) { clip_index = static_cast<int>(i); break; }
  }
  if (clip_index < 0) return { Section::kNoInstance }; // the clip this once referenced no longer exists

  auto & clip = clips[static_cast<size_t>(clip_index)];
  if (!clip.isLooping()) {
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    if (row - event_row >= length) return { Section::kNoInstance }; // one-shot already finished
  }
  return { clip_index, event_row };
}

ActiveInstance
resolveInstanceForBar(const Song & song, const Section & section, int track_id, int bar_start_row, int bar_span) {
  auto & track_instances = section.getInstancesForTrack(track_id);
  if (track_instances.empty()) return { Section::kNoInstance };

  auto bar_last_row = bar_start_row + max(bar_span, 1) - 1;
  auto it = track_instances.upper_bound(static_cast<unsigned short>(bar_last_row));
  if (it == track_instances.begin()) return { Section::kNoInstance }; // nothing at or before this bar's own last row
  --it;
  auto event_row = static_cast<int>(it->first);
  if (event_row < bar_start_row) return resolveInstanceAt(song, section, track_id, bar_start_row); // predates this bar - the ordinary per-row query already covers it correctly, one-shot expiry included

  // This event belongs to this bar - shown unconditionally (one-shot
  // expiry doesn't apply here, unlike resolveInstanceAt(): it was
  // genuinely active for at least part of this bar regardless of what's
  // true by the bar's own last row). An explicit stop landing mid-bar
  // gets the identical treatment, one step further back: whatever it
  // superseded, if that was itself still within this bar (not carried
  // over from an earlier one) and a real clip, was every bit as
  // genuinely active for part of this bar as a one-shot that later
  // expired already is above - only actually falls through to "this bar
  // is stopped" if nothing real preceded the stop within this bar's own
  // span. Only the immediately preceding event is ever checked, not an
  // unbounded walk backward - two stops close enough to leave nothing
  // real in between is degenerate enough that "stopped" is a reasonable
  // answer for it too.
  if (it->second == "OFF" && it != track_instances.begin()) {
    auto prev_it = it;
    --prev_it;
    if (static_cast<int>(prev_it->first) >= bar_start_row && prev_it->second != "OFF") it = prev_it;
  }
  event_row = static_cast<int>(it->first);

  auto & clip_id = it->second;
  if (clip_id == "OFF") return { Section::kStopInstance, event_row };

  // The stored id's own *current* position in the track's clip list -
  // same id-not-position lookup resolveInstanceAt() above already does,
  // for the same reason (Clip.h's own comment on why).
  auto & clips = song.getClips(track_id);
  int clip_index = -1;
  for (size_t i = 0; i < clips.size(); i++) {
    if (clips[i].getId() == clip_id) { clip_index = static_cast<int>(i); break; }
  }
  if (clip_index < 0) return { Section::kNoInstance }; // the clip this once referenced no longer exists
  return { clip_index, event_row };
}

// The clip_index a focused clip resolves to, or -1 if `focused_clip_id`
// is empty or doesn't resolve to a real clip on this track - the same
// id-not-position lookup resolveInstanceAt() already does.
static int
resolveFocusedClipIndex(const Song & song, int track_id, const std::string & focused_clip_id) {
  if (focused_clip_id.empty()) return -1;
  auto & clips = song.getClips(track_id);
  for (size_t i = 0; i < clips.size(); i++) {
    if (clips[i].getId() == focused_clip_id) return static_cast<int>(i);
  }
  return -1;
}

EditTarget
resolveEditTarget(Song & song, Section & section, int track_id, int row, const std::string & focused_clip_id) {
  auto focused_index = resolveFocusedClipIndex(song, track_id, focused_clip_id);
  if (focused_index >= 0) {
    auto & clip = song.getClips(track_id)[static_cast<size_t>(focused_index)];
    if (!clip.hasSample()) {
      auto & pattern = clip.getLeafPattern();
      auto length = clip.getLength() > 0 ? clip.getLength() : 1;
      return { &pattern, pattern.getEffectiveRow(row, length) };
    }
  } else {
    auto active = resolveInstanceAt(song, section, track_id, row);
    if (active.clip_index >= 0) {
      auto & clip = song.getClips(track_id)[static_cast<size_t>(active.clip_index)];
      if (!clip.hasSample()) {
        auto & pattern = clip.getLeafPattern();
        auto length = clip.getLength() > 0 ? clip.getLength() : 1;
        return { &pattern, pattern.getEffectiveRow(row - active.start_row, length) };
      }
    }
  }
  // A SampleTrack's own clip carries raw audio, not a Pattern - there is
  // nothing here to edit at all (no note-column UI exists for it), so
  // this falls back to the section's own (otherwise-unread, for this track)
  // background Pattern, the same as the ordinary "nothing placed here"
  // case just below, rather than handing back the clip's own unused,
  // meaningless Pattern.
  auto & pattern = section.getPatternsByTrack()[track_id];
  return { &pattern, pattern.getEffectiveRow(row, song.getEffectiveSectionLength(section)) };
}

ReadTarget
resolveReadTarget(const Song & song, const Section & section, int track_id, int row, const std::string & focused_clip_id) {
  static const Pattern empty_pattern;
  auto focused_index = resolveFocusedClipIndex(song, track_id, focused_clip_id);
  if (focused_index >= 0) {
    auto & clip = song.getClips(track_id)[static_cast<size_t>(focused_index)];
    // A sample clip's own getLeafPattern() is just an unused, empty
    // Pattern - nothing to read back beyond which clip is focused.
    if (clip.hasSample()) return { &empty_pattern, 0, row, true, focused_index, true };
    auto & pattern = clip.getLeafPattern();
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    return { &pattern, pattern.getEffectiveRow(row, length), row, true, focused_index, true };
  }
  auto active = resolveInstanceAt(song, section, track_id, row);
  if (active.clip_index >= 0) {
    auto & clip = song.getClips(track_id)[static_cast<size_t>(active.clip_index)];
    auto unwrapped_row = row - active.start_row;
    if (clip.hasSample()) return { &empty_pattern, 0, unwrapped_row, true, active.clip_index };
    auto & pattern = clip.getLeafPattern();
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    return { &pattern, pattern.getEffectiveRow(unwrapped_row, length), unwrapped_row, true, active.clip_index };
  }
  auto & patterns = section.getPatternsByTrack();
  auto it = patterns.find(track_id);
  if (it == patterns.end()) return { &empty_pattern, 0, row, false, -1 };
  return { &it->second, it->second.getEffectiveRow(row, song.getEffectiveSectionLength(section)), row, false, -1 };
}
