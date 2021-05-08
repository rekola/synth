#include "Track.h"

#include "SongState.h"

using namespace std;

SampleData
Track::render(size_t frames, SongState & song_state, size_t track_idx, Instrument & instrument, std::map<unsigned int, std::vector<TrackEvent> > & pending_events) {
  size_t num_channels = instrument.getNumChannels();
  assert(num_channels == 1);
  
  SampleData data(num_channels, frames);

  auto & state = song_state.getTrackState(track_idx);
					  
  for (size_t i = 0; i < frames; ) {
    size_t render_size = frames - i;
    if (!pending_events.empty()) {
      auto it = pending_events.begin();
      assert(i <= it->first);
      assert(i == 0 || i == it->first); 
      if (i == it->first) {
	for (auto & ev : it->second) {
	  if (ev.isOff()) {
	    state.stopNote(ev.getId());
	  } else {
	    state.playNote(ev.getId(), ev.getFrequency(), ev.getVelocity(), ev.getDelay(), detune, instrument);
	  }
	}
	it = pending_events.erase(it);
      }
      if (it != pending_events.end() && it->first - i < render_size) render_size = it->first - i;
    }     

    state.renderVoices(data, render_size, i);
    
    i += render_size;
  }

  state.applyEffects(data);

  return data;
}
