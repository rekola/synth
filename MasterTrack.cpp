#include "MasterTrack.h"

#include "Song.h"
#include "SongState.h"
#include "TrackEventQueue.h"

using namespace std;

SampleData
MasterTrack::render(size_t frames, Song & song, SongState & state, TrackEventQueue & track_events) {
  auto & mixer = state.getMixer();
  mixer.reset();

  auto & tracks = getChildren();
  
  for (size_t track_idx = 0; track_idx < tracks.size(); track_idx++) {
    auto & track = tracks[track_idx];
    auto & instrument = song.getInstrument(track.getInstrumentId());
    
    SampleData data = track.render(frames, instrument, track_events.getPendingEvents(track_idx));
    mixer.accumulate(data, track.getVolume(), track.getDistance(), track.getAzimuth(), track.getElevation());
  }
  assert(track_events.empty());

  SampleData master(2, frames);
  
  mixer.encode(master, getVolume());
  applyEffects(master);
  
  return master;
}

