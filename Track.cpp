#include "Track.h"

SampleData
Track::render(size_t frames, Instrument & instrument, std::map<unsigned int, std::vector<TrackEvent> > & pending_events) {
  size_t num_channels = instrument.getNumChannels();
  assert(num_channels == 1);
  
  SampleData data(num_channels, frames);
  auto buffer = data.data();

  for (size_t i = 0; i < frames; ) {
    size_t render_size = frames - i;
    if (!pending_events.empty()) {
      auto it = pending_events.begin();
      assert(i <= it->first);
      assert(i == 0 || i == it->first); 
      if (i == it->first) {
	for (auto & ev : it->second) {
	  if (ev.isOff()) {
	    stopNote(ev.getId());
	  } else {
	    playNote(ev.getFrequency(), ev.getVelocity(), ev.getDelay(), instrument, ev.getId());
	  }
	}
	it = pending_events.erase(it);
      }
      if (it != pending_events.end() && it->first - i < render_size) render_size = it->first - i;
    }     
    
    for (auto & voice : getVoices()) {
      if (voice->isPlaying()) {
	voice->render(buffer, render_size, i);
      }
    }
    
    i += render_size;
  }

  instrument.applyEffects(data);
  applyEffects(data);

  return data;
}
