#include "ArrangementOps.h"

#include "Song.h"
#include "Scene.h"
#include "Clip.h"
#include "Pattern.h"

using namespace std;

void
placeClipInstance(const Song & song, Scene & scene, int track_id, int row, int clip_index) {
  auto & clips = song.getClips(track_id);
  if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return;
  auto & clip = clips[static_cast<size_t>(clip_index)];
  auto length = clip.getLength() > 0 ? clip.getLength() : 1;
  auto reach_end = clip.isLooping() ? song.getPatternLength() - 1 : row + length - 1;

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

EditTarget
resolveEditTarget(Song & song, Scene & scene, int track_id, int row) {
  auto active = resolveInstanceAt(song, scene, track_id, row);
  if (active.clip_index >= 0) {
    auto & clip = song.getClips(track_id)[static_cast<size_t>(active.clip_index)];
    auto & pattern = clip.getLeafPattern();
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    return { &pattern, pattern.getEffectiveRow(row - active.start_row, length) };
  }
  auto & pattern = scene.getPatternsByTrack()[track_id];
  return { &pattern, pattern.getEffectiveRow(row, song.getPatternLength()) };
}

ReadTarget
resolveReadTarget(const Song & song, const Scene & scene, int track_id, int row) {
  static const Pattern empty_pattern;
  auto active = resolveInstanceAt(song, scene, track_id, row);
  if (active.clip_index >= 0) {
    auto & clip = song.getClips(track_id)[static_cast<size_t>(active.clip_index)];
    auto & pattern = clip.getLeafPattern();
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    auto unwrapped_row = row - active.start_row;
    return { &pattern, pattern.getEffectiveRow(unwrapped_row, length), unwrapped_row, true };
  }
  auto & patterns = scene.getPatternsByTrack();
  auto it = patterns.find(track_id);
  if (it == patterns.end()) return { &empty_pattern, 0, row, false };
  return { &it->second, it->second.getEffectiveRow(row, song.getPatternLength()), row, false };
}
