#include "Effect.h"

#include "SongState.h"

using namespace std;

SampleData
Effect::render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events) {
  auto sd = Track::render(frames, song_state, instruments, events);
  song_state.getTrackState(*this).apply(sd);  
  return sd;
}
