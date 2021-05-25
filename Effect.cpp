#include "Effect.h"

#include "SongState.h"

using namespace std;

SampleData
Effect::render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Instrument> > & instruments, TrackEventQueue & events) {
  SampleData sd;
  if (getChildren().empty()) {
    sd = SampleData(1, frames);
  } else {
    auto it = getChildren().begin();
    sd = (*it)->render(frames, song_state, instruments, events);
    for (it++; it != getChildren().end(); it++) {
      auto sd2 = (*it)->render(frames, song_state, instruments, events);
      sd.mix(sd2);
    }
  }
  
  song_state.getEffectState(*this).apply(sd);
  
  return sd;
}
