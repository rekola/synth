#ifndef _SESSIONTRACKINFO_H_
#define _SESSIONTRACKINFO_H_

#include <unordered_map>

// What Session view has done to one track: whether a launched clip (or a
// stop) has taken it over from the arrangement, and what's queued for the
// next bar. Kept by SongState on the audio thread and mirrored to the UI
// in each playback snapshot.
struct SessionTrackInfo {
  static constexpr int kArrangement = -1; // following the arrangement
  static constexpr int kSilent = -2; // taken over, playing nothing
  static constexpr int kNothingQueued = -3;

  int clip_index = kArrangement; // a launched clip, kSilent or kArrangement
  int launch_clock = 0; // the session clock at the launched clip's row 0
  int queued = kNothingQueued; // a clip index, kSilent or kArrangement, taking effect at the next bar

  bool isTakenOver() const { return clip_index != kArrangement; }
  bool isIdle() const { return clip_index == kArrangement && queued == kNothingQueued; }
};

using SessionTracks = std::unordered_map<int, SessionTrackInfo>;

// Queues `target` (a clip index, kSilent, kArrangement, or kNothingQueued
// to cancel) for `track_id` - the one rule both SongState and the UI's
// prediction of it apply.
inline void queueSessionChange(SessionTracks & tracks, int track_id, int target) {
  auto & track = tracks[track_id];
  track.queued = target == SessionTrackInfo::kArrangement && !track.isTakenOver() ? SessionTrackInfo::kNothingQueued : target;
  if (track.isIdle()) tracks.erase(track_id);
}

// Stops every launched clip at once and forgets everything queued; the
// tracks stay taken over, silent.
inline void silenceSessionTracks(SessionTracks & tracks) {
  for (auto it = tracks.begin(); it != tracks.end(); ) {
    auto & track = it->second;
    if (track.clip_index >= 0) track.clip_index = SessionTrackInfo::kSilent;
    track.queued = SessionTrackInfo::kNothingQueued;
    if (track.isIdle()) it = tracks.erase(it);
    else ++it;
  }
}

#endif
