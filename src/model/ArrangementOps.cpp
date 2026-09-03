#include "ArrangementOps.h"

#include "Song.h"
#include "Scene.h"
#include "Clip.h"
#include "Pattern.h"

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
placeClipInstance(const Song & song, Scene & scene, int track_id, int row, int clip_index) {
  auto & clips = song.getClips(track_id);
  if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return;
  auto & clip = clips[static_cast<size_t>(clip_index)];
  auto length = clip.getLength() > 0 ? clip.getLength() : 1;
  auto reach_end = clip.isLooping() ? song.getEffectiveSceneLength(scene) - 1 : row + length - 1;

  // Collected first, then cleared in a separate pass - clearInstance()
  // mutates the same map getInstancesForTrack() returns a reference
  // into, so erasing while iterating it directly would be unsafe.
  vector<int> rows_to_clear;
  for (auto & [ existing_row, existing_clip_id ] : scene.getInstancesForTrack(track_id)) {
    if (existing_row >= row && existing_row <= reach_end) rows_to_clear.push_back(existing_row);
  }
  for (auto r : rows_to_clear) scene.clearInstance(track_id, r);

  // Stores the clip's own stable id, not `clip_index` itself - Clip.h's
  // own comment on why.
  scene.setInstance(track_id, row, clip.getId());
}

void
placeStopInstance(Scene & scene, int track_id, int row) {
  scene.setInstance(track_id, row, "OFF");
}

void
deleteClip(Song & song, int track_id, int clip_index) {
  auto & clips = song.getClips(track_id);
  if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return;
  auto clip_id = clips[static_cast<size_t>(clip_index)].getId();

  // Every scene, not just the one(s) the caller happens to know about -
  // the same clip can be (and, live-linked editing being the whole point
  // of a clip, often is) placed in several.
  for (size_t i = 0; i < song.getScenes().size(); i++) {
    auto & scene = song.getScene(static_cast<int>(i));
    // Collected first, then cleared in a separate pass - same reasoning
    // placeClipInstance() above already documents.
    vector<int> rows_to_clear;
    for (auto & [ row, existing_clip_id ] : scene.getInstancesForTrack(track_id)) {
      if (existing_clip_id == clip_id) rows_to_clear.push_back(row);
    }
    for (auto row : rows_to_clear) scene.clearInstance(track_id, row);
  }

  clips.erase(clips.begin() + clip_index);
  song.incVersion();
}

ActiveInstance
resolveInstanceAt(const Song & song, const Scene & scene, int track_id, int row) {
  auto & track_instances = scene.getInstancesForTrack(track_id);
  if (track_instances.empty()) return { Scene::kNoInstance };

  auto it = track_instances.upper_bound(static_cast<unsigned short>(row));
  if (it == track_instances.begin()) return { Scene::kNoInstance }; // nothing at or before row
  --it;
  auto event_row = static_cast<int>(it->first);
  auto & clip_id = it->second;
  if (clip_id == "OFF") return { Scene::kStopInstance, event_row };

  // The stored id's own *current* position in the track's clip list -
  // never assumed to still be wherever it was when the instance was
  // placed (Clip.h's own comment on why).
  auto & clips = song.getClips(track_id);
  int clip_index = -1;
  for (size_t i = 0; i < clips.size(); i++) {
    if (clips[i].getId() == clip_id) { clip_index = static_cast<int>(i); break; }
  }
  if (clip_index < 0) return { Scene::kNoInstance }; // the clip this once referenced no longer exists

  auto & clip = clips[static_cast<size_t>(clip_index)];
  if (!clip.isLooping()) {
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    if (row - event_row >= length) return { Scene::kNoInstance }; // one-shot already finished
  }
  return { clip_index, event_row };
}

ActiveInstance
resolveInstanceForBar(const Song & song, const Scene & scene, int track_id, int bar_start_row, int bar_span) {
  auto & track_instances = scene.getInstancesForTrack(track_id);
  if (track_instances.empty()) return { Scene::kNoInstance };

  auto bar_last_row = bar_start_row + max(bar_span, 1) - 1;
  auto it = track_instances.upper_bound(static_cast<unsigned short>(bar_last_row));
  if (it == track_instances.begin()) return { Scene::kNoInstance }; // nothing at or before this bar's own last row
  --it;
  auto event_row = static_cast<int>(it->first);
  if (event_row < bar_start_row) return resolveInstanceAt(song, scene, track_id, bar_start_row); // predates this bar - the ordinary per-row query already covers it correctly, one-shot expiry included

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
  if (clip_id == "OFF") return { Scene::kStopInstance, event_row };

  // The stored id's own *current* position in the track's clip list -
  // same id-not-position lookup resolveInstanceAt() above already does,
  // for the same reason (Clip.h's own comment on why).
  auto & clips = song.getClips(track_id);
  int clip_index = -1;
  for (size_t i = 0; i < clips.size(); i++) {
    if (clips[i].getId() == clip_id) { clip_index = static_cast<int>(i); break; }
  }
  if (clip_index < 0) return { Scene::kNoInstance }; // the clip this once referenced no longer exists
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
resolveEditTarget(Song & song, Scene & scene, int track_id, int row, const std::string & focused_clip_id) {
  auto focused_index = resolveFocusedClipIndex(song, track_id, focused_clip_id);
  if (focused_index >= 0) {
    auto & clip = song.getClips(track_id)[static_cast<size_t>(focused_index)];
    if (!clip.hasSample()) {
      auto & pattern = clip.getLeafPattern();
      auto length = clip.getLength() > 0 ? clip.getLength() : 1;
      return { &pattern, pattern.getEffectiveRow(row, length) };
    }
  } else {
    auto active = resolveInstanceAt(song, scene, track_id, row);
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
  // this falls back to the scene's own (otherwise-unread, for this track)
  // background Pattern, the same as the ordinary "nothing placed here"
  // case just below - safe, if this is ever actually reached, rather than
  // dereferencing a Pattern the clip was never given one of.
  auto & pattern = scene.getPatternsByTrack()[track_id];
  return { &pattern, pattern.getEffectiveRow(row, song.getEffectiveSceneLength(scene)) };
}

ReadTarget
resolveReadTarget(const Song & song, const Scene & scene, int track_id, int row, const std::string & focused_clip_id) {
  static const Pattern empty_pattern;
  auto focused_index = resolveFocusedClipIndex(song, track_id, focused_clip_id);
  if (focused_index >= 0) {
    auto & clip = song.getClips(track_id)[static_cast<size_t>(focused_index)];
    // A sample clip has no leaf Pattern at all (Clip::getLeafPattern()
    // would throw) - nothing to read back beyond which clip is focused.
    if (clip.hasSample()) return { &empty_pattern, 0, row, true, focused_index, true };
    auto & pattern = clip.getLeafPattern();
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    return { &pattern, pattern.getEffectiveRow(row, length), row, true, focused_index, true };
  }
  auto active = resolveInstanceAt(song, scene, track_id, row);
  if (active.clip_index >= 0) {
    auto & clip = song.getClips(track_id)[static_cast<size_t>(active.clip_index)];
    auto unwrapped_row = row - active.start_row;
    if (clip.hasSample()) return { &empty_pattern, 0, unwrapped_row, true, active.clip_index };
    auto & pattern = clip.getLeafPattern();
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    return { &pattern, pattern.getEffectiveRow(unwrapped_row, length), unwrapped_row, true, active.clip_index };
  }
  auto & patterns = scene.getPatternsByTrack();
  auto it = patterns.find(track_id);
  if (it == patterns.end()) return { &empty_pattern, 0, row, false, -1 };
  return { &it->second, it->second.getEffectiveRow(row, song.getEffectiveSceneLength(scene)), row, false, -1 };
}
