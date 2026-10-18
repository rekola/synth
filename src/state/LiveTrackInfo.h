#ifndef _LIVETRACKINFO_H_
#define _LIVETRACKINFO_H_

#include <algorithm>
#include <unordered_map>

// What Live View has done to one track: whether a launched clip (or a
// stop) has taken it over from the arrangement, and what's queued for the
// next bar. Kept by SongState on the audio thread and mirrored to the UI
// in each playback snapshot.
struct LiveTrackInfo {
  static constexpr int kArrangement = -1; // following the arrangement
  static constexpr int kSilent = -2; // taken over, playing nothing
  static constexpr int kNothingQueued = -3;

  int clip_index = kArrangement; // a launched clip, kSilent or kArrangement
  int launch_clock = 0; // the live clock at the launched clip's row 0
  int queued = kNothingQueued; // a clip index, kSilent or kArrangement, taking effect at the next bar

  bool isTakenOver() const { return clip_index != kArrangement; }
  bool isIdle() const { return clip_index == kArrangement && queued == kNothingQueued; }
};

using LiveTracks = std::unordered_map<int, LiveTrackInfo>;

// Queues `target` (a clip index, kSilent, kArrangement, or kNothingQueued
// to cancel) for `track_id` - the one rule both SongState and the UI's
// prediction of it apply.
inline void queueLaunch(LiveTracks & tracks, int track_id, int target) {
  auto & track = tracks[track_id];
  track.queued = target == LiveTrackInfo::kArrangement && !track.isTakenOver() ? LiveTrackInfo::kNothingQueued : target;
  if (track.isIdle()) tracks.erase(track_id);
}

// Moves every launched clip's playhead by `delta` rows at live clock
// `clock`, none before its row 0 - the one rule SongState and the UI's
// prediction of it share.
inline void shiftLiveTracks(LiveTracks & tracks, int clock, int delta) {
  for (auto & [ track_id, track ] : tracks) {
    if (track.clip_index < 0) continue;
    track.launch_clock = clock - std::max(0, clock - track.launch_clock + delta);
  }
}

// Stops every launched clip at once and forgets everything queued; the
// tracks stay taken over, silent.
inline void silenceLiveTracks(LiveTracks & tracks) {
  for (auto it = tracks.begin(); it != tracks.end(); ) {
    auto & track = it->second;
    if (track.clip_index >= 0) track.clip_index = LiveTrackInfo::kSilent;
    track.queued = LiveTrackInfo::kNothingQueued;
    if (track.isIdle()) it = tracks.erase(it);
    else ++it;
  }
}

#endif
