#include "ArrangementOps.h"

#include "Song.h"
#include "Arrangement.h"
#include "Clip.h"
#include "Pattern.h"
#include "PercussionTrack.h"
#include "SampleTrack.h"
#include "../dsp/HashField.h"
#include "../instruments/Tuning.h"
#include "SampleContent.h"
#include "../audio/AudioBuffer.h"
#include "../ambisonic/ChannelConfiguration.h"

#include <algorithm>
#include <map>

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
placeClipInstance(Song & song, int track_id, int row, int clip_index) {
  auto & clips = song.getClips(track_id);
  if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return;
  auto & clip = clips[static_cast<size_t>(clip_index)];
  auto length = clip.getLength() > 0 ? clip.getLength() : 1;
  auto reach_end = row + length - 1;
  auto & arrangement = song.getArrangement();

  // Collected first, then cleared in a separate pass - clearInstance()
  // mutates the same map getInstancesForTrack() returns a reference
  // into, so erasing while iterating it directly would be unsafe.
  vector<int> rows_to_clear;
  for (auto & [ existing_row, existing_clip_id ] : arrangement.getInstancesForTrack(track_id)) {
    if (existing_row >= row && existing_row <= reach_end) rows_to_clear.push_back(existing_row);
  }
  for (auto r : rows_to_clear) arrangement.clearInstance(track_id, r);

  // Stores the clip's own stable id, not `clip_index` itself - Clip.h's
  // own comment on why.
  arrangement.setInstance(track_id, row, clip.getId());
}

void
placeStopInstance(Song & song, int track_id, int row) {
  song.getArrangement().setInstance(track_id, row, "OFF");
}

bool
mergeClipToBackground(Song & song, int track_id, int row, const ChannelConfiguration & channel_config) {
  auto active = resolveInstanceAt(song, track_id, row);
  if (active.clip_index < 0) return false; // nothing real placed here

  auto & arrangement = song.getArrangement();
  auto & clip = song.getClips(track_id)[static_cast<size_t>(active.clip_index)];
  auto length = clip.getLength() > 0 ? clip.getLength() : 1;
  // A one-shot covers its own length; a looping clip plays on until the
  // track's next event, or the arrangement's end.
  auto reach_end = active.start_row + length - 1;
  if (clip.isLooping()) {
    auto & instances = arrangement.getInstancesForTrack(track_id);
    auto next = instances.upper_bound(static_cast<unsigned short>(active.start_row));
    reach_end = next != instances.end() ? static_cast<int>(next->first) - 1 : max(reach_end, song.getArrangementLength() - 1);
  }

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

    auto sample_interval = channel_config.getSampleInterval(song_tempo);
    auto needed_frames = static_cast<int64_t>(reach_end + 1) * sample_interval;
    auto & background = arrangement.getOrCreateSampleBackgroundContent(track_id);

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
    auto & background = arrangement.getPatternsByTrack()[track_id];
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

  placeStopInstance(song, track_id, active.start_row);
  return true;
}

void
deleteClip(Song & song, int track_id, int clip_index) {
  auto & clips = song.getClips(track_id);
  if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return;
  auto clip_id = clips[static_cast<size_t>(clip_index)].getId();

  // Every placement - the same clip can be (and, live-linked editing
  // being the whole point of a clip, often is) placed several times.
  // Collected first, then cleared in a separate pass - same reasoning
  // placeClipInstance() above already documents.
  auto & arrangement = song.getArrangement();
  vector<int> rows_to_clear;
  for (auto & [ row, existing_clip_id ] : arrangement.getInstancesForTrack(track_id)) {
    if (existing_clip_id == clip_id) rows_to_clear.push_back(row);
  }
  for (auto row : rows_to_clear) arrangement.clearInstance(track_id, row);

  // Reset in place to a fresh, id-less filler (Song::ensureClipAt()'s own
  // "hole" state), never erased outright - holes are allowed, and every
  // other track's own scene rows are indexed against this same track's
  // clip list, so shifting everything past the deleted one down (the way
  // a plain vector erase would) would silently misalign them all against
  // it, even when the deletion happened on a completely different track.
  clips[static_cast<size_t>(clip_index)] = Clip(track_id);
  song.incVersion();
}

int
duplicateClip(Song & song, int track_id, int from_index, int to_index) {
  auto & clips = song.getClips(track_id);
  if (from_index < 0 || from_index >= static_cast<int>(clips.size()) || clips[static_cast<size_t>(from_index)].isEmpty()) return -1;
  if (to_index < 0) {
    to_index = from_index + 1;
    while (to_index < static_cast<int>(clips.size()) && !clips[static_cast<size_t>(to_index)].isEmpty()) to_index++;
  } else if (to_index < static_cast<int>(clips.size()) && !clips[static_cast<size_t>(to_index)].isEmpty()) {
    return -1;
  }
  Clip copy = clips[static_cast<size_t>(from_index)];
  copy.setId(song.generateUniqueClipId());
  song.ensureClipAt(track_id, to_index); // may reallocate the list
  song.getClips(track_id)[static_cast<size_t>(to_index)] = std::move(copy);
  song.incVersion();
  return to_index;
}

namespace {

// `value` moved `steps` scale members in `direction` (+1/-1); -1 when that
// would leave the note range.
int
stepInScale(int value, int steps, int direction, const vector<bool> & in_scale) {
  auto edo = static_cast<int>(in_scale.size());
  for (int s = 0; s < steps; s++) {
    for (int guard = 0; guard <= edo; guard++) {
      value += direction;
      if (value < 0) return -1;
      if (in_scale[static_cast<size_t>(value % edo)]) break;
    }
  }
  return value;
}

constexpr HashField kMutateField(0x6d75746174650001ull);

// Uniform in [0, n) for one note (or the whole call, with a zero cell).
int
mutatePick(uint32_t seed, int cell, const char * axis, int n) {
  auto coord = (static_cast<int64_t>(seed) << 24) | cell;
  return min(n - 1, static_cast<int>(kMutateField.unit(coord, paramId(axis)) * static_cast<float>(n)));
}

}

int
mutateClip(Song & song, int track_id, int clip_index, uint32_t seed) {
  auto & clips = song.getClips(track_id);
  if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return 0;
  auto & clip = clips[static_cast<size_t>(clip_index)];
  auto & pattern = clip.getLeafPattern();
  if (pattern.isEmpty()) return 0; // a sample clip's content isn't notes

  auto * percussion = dynamic_cast<const PercussionTrack *>(song.getMasterTrack().getChildByInternalId(track_id));
  vector<int> lanes;
  vector<bool> in_scale;
  if (percussion) {
    lanes = percussion->getLaneNotes();
    if (lanes.size() < 2) return 0;
  } else {
    auto edo = edoStepsFor(song.getTuning());
    if (edo <= 0) return 0;
    in_scale.assign(static_cast<size_t>(edo), false);
    for (auto degree : song.getScaleDegreesWindow(0, 64)) in_scale[static_cast<size_t>(((degree % edo) + edo) % edo)] = true;
  }

  // Working copy of the notes, written back once at the end.
  map<int, vector<Note>> grid;
  for (auto & [ row, notes ] : pattern.getNotesByRow()) grid[row] = notes;
  auto row_limit = clip.getLength() > 0 ? clip.getLength() : pattern.getContentEnd();
  auto is_note_on = [](const Note & n) { return n.getValue() >= 0 && n.getVelocity() > 0; };
  auto cell = [&](int row, size_t col) -> const Note * {
    auto it = grid.find(row);
    return it != grid.end() && col < it->second.size() ? &it->second[col] : nullptr;
  };
  auto is_free = [&](int row, size_t col) {
    auto * n = cell(row, col);
    return row >= 0 && row < row_limit && (!n || !n->isDefined());
  };
  auto put = [&](int row, size_t col, const Note & note) {
    auto & columns = grid[row];
    if (columns.size() <= col) columns.resize(col + 1);
    columns[col] = note;
  };
  auto remove = [&](int row, size_t col) {
    auto it = grid.find(row);
    if (it == grid.end() || col >= it->second.size()) return;
    it->second[col] = Note();
    while (!it->second.empty() && !it->second.back().isDefined()) it->second.pop_back();
    if (it->second.empty()) grid.erase(it);
  };
  // The row of the off that ends the note-on at (row, col), or -1 when it has none.
  auto off_row_for = [&](int row, size_t col) {
    auto value = cell(row, col)->getValue();
    for (auto it = grid.upper_bound(row); it != grid.end(); ++it) {
      if (col >= it->second.size()) continue;
      auto & n = it->second[col];
      if (n.getValue() != value) continue;
      return n.isOff() ? it->first : -1;
    }
    return -1;
  };

  struct Position { int row; size_t col; };
  vector<Position> note_ons;
  for (auto & [ row, notes ] : grid) {
    for (size_t col = 0; col < notes.size(); col++) if (is_note_on(notes[col])) note_ons.push_back({ row, col });
  }
  if (note_ons.empty()) return 0;

  auto remaining = static_cast<int>(note_ons.size());
  auto forced = mutatePick(seed, 0, "forced", remaining);
  constexpr int kOps = 4;
  enum Op { PITCH, MOVE, DROP, RATCHET };
  int changed = 0;
  for (size_t i = 0; i < note_ons.size(); i++) {
    auto [ row, col ] = note_ons[i];
    auto key = (row << 8) | static_cast<int>(col);
    auto is_forced = static_cast<int>(i) == forced;
    if (!is_forced && mutatePick(seed, key, "picked", 4) != 0) continue;
    auto note = *cell(row, col);
    auto off_row = off_row_for(row, col);
    auto off_note = off_row >= 0 ? *cell(off_row, col) : Note();

    // Tries one op; false when it had nothing to do here (a blocked move, a lone note to drop).
    auto apply = [&](int op) {
      switch (op) {
      case PITCH: {
	int value = -1;
	if (percussion) {
	  vector<int> others;
	  for (auto lane : lanes) if (lane != note.getValue()) others.push_back(lane);
	  if (!others.empty()) value = others[static_cast<size_t>(mutatePick(seed, key, "lane", static_cast<int>(others.size())))];
	} else {
	  auto steps = 1 + mutatePick(seed, key, "steps", 2);
	  auto direction = mutatePick(seed, key, "direction", 2) ? 1 : -1;
	  value = stepInScale(note.getValue(), steps, direction, in_scale);
	  if (value < 0) value = stepInScale(note.getValue(), steps, -direction, in_scale);
	}
	if (value < 0 || value == note.getValue()) return false;
	put(row, col, Note(value, note.getVelocity(), note.getDelay()));
	if (off_row >= 0) put(off_row, col, Note(value, 0, off_note.getDelay()));
	return true;
      }
      case MOVE: {
	auto shift = mutatePick(seed, key, "direction", 2) ? 1 : -1;
	remove(row, col);
	if (off_row >= 0) remove(off_row, col);
	// The note keeps its length; the target cells are checked with it lifted out.
	if (!is_free(row + shift, col) || (off_row >= 0 && !is_free(off_row + shift, col))) {
	  put(row, col, note);
	  if (off_row >= 0) put(off_row, col, off_note);
	  return false;
	}
	put(row + shift, col, note);
	if (off_row >= 0) put(off_row + shift, col, off_note);
	return true;
      }
      case DROP:
	if (remaining <= 1) return false; // never empty the clip
	remove(row, col);
	if (off_row >= 0) remove(off_row, col);
	remaining--;
	return true;
      default: {
	// Extra hits of the same sound inside the row, in free note columns, softer.
	constexpr size_t kMaxColumns = 4;
	auto hits = 2 + mutatePick(seed, key, "hits", 3); // 2..4 hits in all
	int added = 0;
	for (int h = 1; h < hits; h++) {
	  auto delay = note.getDelay() + h * 256 / hits;
	  if (delay > 255) break;
	  size_t free_col = 0;
	  while (free_col < kMaxColumns && !is_free(row, free_col)) free_col++;
	  if (free_col >= kMaxColumns) break;
	  put(row, free_col, Note(note.getValue(), static_cast<short>(max(1, note.getVelocity() * 3 / 4)), static_cast<short>(delay)));
	  added++;
	}
	return added > 0;
      }
      }
    };

    // Weighted first choice: a pitched track mostly changes pitch, a
    // percussion track mostly repeats, moves or thins its hits. The note
    // the call guarantees to touch falls through to the other ops when its
    // first choice has nothing to do.
    auto roll = mutatePick(seed, key, "op", 8);
    int first;
    if (percussion) first = roll < 1 ? PITCH : roll < 3 ? MOVE : roll < 5 ? DROP : RATCHET;
    else first = roll < 5 ? PITCH : roll < 7 ? MOVE : DROP;
    for (int attempt = 0; attempt < (is_forced && changed == 0 ? kOps : 1); attempt++) {
      auto op = (first + attempt) % kOps;
      if (!percussion && op == RATCHET) continue;
      if (apply(op)) { changed++; break; }
    }
  }

  if (changed == 0) return 0;
  vector<int> stale;
  for (auto & [ row, notes ] : pattern.getNotesByRow()) if (!grid.count(row)) stale.push_back(row);
  for (auto row : stale) pattern.clearNotes(row);
  for (auto & [ row, notes ] : grid) pattern.setNotes(row, notes);
  song.incVersion();
  return changed;
}

ActiveInstance
resolveInstanceAt(const Song & song, int track_id, int row) {
  auto & track_instances = song.getArrangement().getInstancesForTrack(track_id);
  if (track_instances.empty()) return { Arrangement::kNoInstance };

  auto it = track_instances.upper_bound(static_cast<unsigned short>(row));
  if (it == track_instances.begin()) return { Arrangement::kNoInstance }; // nothing at or before row
  --it;
  auto event_row = static_cast<int>(it->first);
  auto & clip_id = it->second;
  if (clip_id == "OFF") return { Arrangement::kStopInstance, event_row };

  // The stored id's own *current* position in the track's clip list -
  // never assumed to still be wherever it was when the instance was
  // placed (Clip.h's own comment on why).
  auto & clips = song.getClips(track_id);
  int clip_index = -1;
  for (size_t i = 0; i < clips.size(); i++) {
    if (clips[i].getId() == clip_id) { clip_index = static_cast<int>(i); break; }
  }
  if (clip_index < 0) return { Arrangement::kNoInstance }; // the clip this once referenced no longer exists

  auto & clip = clips[static_cast<size_t>(clip_index)];
  if (!clip.isLooping()) {
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    if (row - event_row >= length) return { Arrangement::kNoInstance }; // one-shot already finished
  }
  return { clip_index, event_row };
}

ActiveInstance
resolveInstanceForBar(const Song & song, int track_id, int bar_start_row, int bar_span) {
  auto & track_instances = song.getArrangement().getInstancesForTrack(track_id);
  if (track_instances.empty()) return { Arrangement::kNoInstance };

  auto bar_last_row = bar_start_row + max(bar_span, 1) - 1;
  auto it = track_instances.upper_bound(static_cast<unsigned short>(bar_last_row));
  if (it == track_instances.begin()) return { Arrangement::kNoInstance }; // nothing at or before this bar's own last row
  --it;
  auto event_row = static_cast<int>(it->first);
  if (event_row < bar_start_row) return resolveInstanceAt(song, track_id, bar_start_row); // predates this bar - the ordinary per-row query already covers it correctly, one-shot expiry included

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
  if (clip_id == "OFF") return { Arrangement::kStopInstance, event_row };

  // The stored id's own *current* position in the track's clip list -
  // same id-not-position lookup resolveInstanceAt() above already does,
  // for the same reason (Clip.h's own comment on why).
  auto & clips = song.getClips(track_id);
  int clip_index = -1;
  for (size_t i = 0; i < clips.size(); i++) {
    if (clips[i].getId() == clip_id) { clip_index = static_cast<int>(i); break; }
  }
  if (clip_index < 0) return { Arrangement::kNoInstance }; // the clip this once referenced no longer exists
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
resolveEditTarget(Song & song, int track_id, int row, const std::string & focused_clip_id) {
  auto focused_index = resolveFocusedClipIndex(song, track_id, focused_clip_id);
  if (focused_index >= 0) {
    auto & clip = song.getClips(track_id)[static_cast<size_t>(focused_index)];
    if (!clip.hasSample()) {
      auto & pattern = clip.getLeafPattern();
      auto length = clip.getLength() > 0 ? clip.getLength() : 1;
      return { &pattern, pattern.getEffectiveRow(row, length) };
    }
  } else {
    auto active = resolveInstanceAt(song, track_id, row);
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
  // this falls back to the track's own (otherwise-unread, for this track)
  // background Pattern, the same as the ordinary "nothing placed here"
  // case just below, rather than handing back the clip's own unused,
  // meaningless Pattern.
  auto & pattern = song.getArrangement().getPatternsByTrack()[track_id];
  return { &pattern, pattern.getEffectiveRow(row, 0) };
}

ReadTarget
resolveReadTarget(const Song & song, int track_id, int row, const std::string & focused_clip_id) {
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
  auto active = resolveInstanceAt(song, track_id, row);
  if (active.clip_index >= 0) {
    auto & clip = song.getClips(track_id)[static_cast<size_t>(active.clip_index)];
    auto unwrapped_row = row - active.start_row;
    if (clip.hasSample()) return { &empty_pattern, 0, unwrapped_row, true, active.clip_index };
    auto & pattern = clip.getLeafPattern();
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    return { &pattern, pattern.getEffectiveRow(unwrapped_row, length), unwrapped_row, true, active.clip_index };
  }
  auto & patterns = song.getArrangement().getPatternsByTrack();
  auto it = patterns.find(track_id);
  if (it == patterns.end()) return { &empty_pattern, 0, row, false, -1 };
  return { &it->second, it->second.getEffectiveRow(row, 0), row, false, -1 };
}

std::vector<int>
getHitLaneValues(const Pattern & pattern, int effective_row, const std::vector<int> & lane_values) {
  std::vector<int> hits;
  auto & notes = pattern.getNotes(effective_row);
  for (auto lane_value : lane_values) {
    for (auto & note : notes) {
      if (note.isDefined() && !note.isOff() && !note.isAftertouch() && note.getValue() == lane_value) {
        hits.push_back(lane_value);
        break;
      }
    }
  }
  return hits;
}
