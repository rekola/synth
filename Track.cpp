#include "Track.h"

#include "SongState.h"

SampleData
Track::render(int frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events) const {
  bool child_has_solo = false;
  for (auto & child : getChildren()) {
    if (child->isSolo()) {
      child_has_solo = true;
      break;
    }
  }

  SampleData sd(song_state.getChannelConfiguration(), frames, isSolo() || child_has_solo);
  sd.zero();
  
  for (auto & child : getChildren()) {
    auto sd2 = child->render(frames, song_state, instruments, events);
    if (!child->isMuted() && (!child_has_solo || child->isSolo())) {
      sd.mix(sd2, child->getVolume());
    }
  }
  return sd;
}
