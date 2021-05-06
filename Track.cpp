#include "Track.h"

#include "SongState.h"

using namespace std;

SampleData
Track::render(size_t frames, SongState & state, size_t track_idx, Instrument & instrument, std::map<unsigned int, std::vector<TrackEvent> > & pending_events) {
  size_t num_channels = instrument.getNumChannels();
  assert(num_channels == 1);
  
  SampleData data(num_channels, frames);

  for (size_t i = 0; i < frames; ) {
    size_t render_size = frames - i;
    if (!pending_events.empty()) {
      auto it = pending_events.begin();
      assert(i <= it->first);
      assert(i == 0 || i == it->first); 
      if (i == it->first) {
	for (auto & ev : it->second) {
	  if (ev.isOff()) {
	    state.stopNote(track_idx, ev.getId());
	  } else {
	    state.playNote(track_idx, ev.getId(), ev.getFrequency(), ev.getVelocity(), detune, ev.getDelay(), instrument);
	  }
	}
	it = pending_events.erase(it);
      }
      if (it != pending_events.end() && it->first - i < render_size) render_size = it->first - i;
    }     
    
    for (auto & voice : state.getVoices(track_idx)) {
      if (voice->isPlaying()) {
	auto voice_data = voice->render(render_size);
	data.mix(voice_data, i);	
      }
    }
    
    i += render_size;
  }

  applyEffects(data);

  return data;
}
