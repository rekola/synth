#include "ArrangementOps.h"

#include "Song.h"
#include "Scene.h"
#include "Clip.h"

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
  for (auto & [ existing_row, existing_clip_index ] : scene.getInstancesForTrack(track_id)) {
    if (existing_row >= row && existing_row <= reach_end) rows_to_clear.push_back(existing_row);
  }
  for (auto r : rows_to_clear) scene.clearInstance(track_id, r);

  scene.setInstance(track_id, row, clip_index);
}

void
placeStopInstance(Scene & scene, int track_id, int row) {
  scene.setInstance(track_id, row, Scene::kStopInstance);
}

ActiveInstance
resolveInstanceAt(const Song & song, const Scene & scene, int track_id, int row) {
  auto & track_instances = scene.getInstancesForTrack(track_id);
  if (track_instances.empty()) return { Scene::kNoInstance };

  auto it = track_instances.upper_bound(static_cast<unsigned short>(row));
  if (it == track_instances.begin()) return { Scene::kNoInstance }; // nothing at or before row
  --it;
  auto event_row = static_cast<int>(it->first);
  auto clip_index = it->second;
  if (clip_index == Scene::kStopInstance) return { Scene::kStopInstance, event_row };

  auto & clips = song.getClips(track_id);
  if (clip_index < 0 || clip_index >= static_cast<int>(clips.size())) return { Scene::kNoInstance };
  auto & clip = clips[static_cast<size_t>(clip_index)];
  if (!clip.isLooping()) {
    auto length = clip.getLength() > 0 ? clip.getLength() : 1;
    if (row - event_row >= length) return { Scene::kNoInstance }; // one-shot already finished
  }
  return { clip_index, event_row };
}
