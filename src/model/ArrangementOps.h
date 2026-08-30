#ifndef _ARRANGEMENTOPS_H_
#define _ARRANGEMENTOPS_H_

class Song;
class Scene;

// Places a real-clip instance event (clip_index - the clip's own ordinal
// position in track_id's own clip list, Song::getClips(track_id)) at
// `row` on `track_id` in `scene`. Clears away any other instance event
// already placed on that same track within this clip's own reach first
// (Scene::clearInstance() - "removed outright, not left as unreachable
// data"): through row + the clip's own native length if it's a one-shot
// (it has a real, known duration - clearing further would destroy later
// placements it was never going to touch), through the scene's own last
// row if it's looping (no natural bound of its own, so no shorter
// boundary to respect). A no-op if clip_index doesn't resolve to a real
// clip in that track's own list.
void placeClipInstance(const Song & song, Scene & scene, int track_id, int row, int clip_index);

// Places an explicit stop ("instantiate nothing") at `row` on `track_id`
// in `scene`. Clears nothing, unlike placeClipInstance() above - a stop
// adds no new sounding content, so there's nothing of its own to protect
// going forward; whatever's already there from `row` onward was already
// left in a consistent state by whatever was placed before it.
void placeStopInstance(Scene & scene, int track_id, int row);

// The result of resolveInstanceAt() below. `start_row` is the resolved
// instance event's own row - only meaningful when `clip_index` is a real
// clip, but a caller needs it there: rendering that clip's own content
// at `row` means reading it at (row - start_row), wrapped by the clip's
// own length (Pattern::getEffectiveRow()), not at `row` itself.
struct ActiveInstance {
  int clip_index; // a real clip's own ordinal index, Scene::kStopInstance, or Scene::kNoInstance
  int start_row = 0;
};

// Resolves what's active at (track_id, row) in `scene` - the same query
// real (transport) playback's own scheduler needs (SongState.h's own
// renderBlock(), hence this living in src/model/ rather than src/ui/
// alongside PatternBlockOps.h - real playback can't depend on anything
// UI-adjacent). Scans backward for the most recent instance event at or
// before `row`; that event's own clip_index if a real clip is still
// active there (accounting for a one-shot's own native length having
// run out), Scene::kStopInstance if the most recent event is an
// explicit stop, or Scene::kNoInstance if nothing was ever placed on
// this track at or before `row` at all (or a stale clip_index no longer
// resolves to a real clip).
ActiveInstance resolveInstanceAt(const Song & song, const Scene & scene, int track_id, int row);

#endif
