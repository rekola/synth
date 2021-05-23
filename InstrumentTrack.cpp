#include "InstrumentTrack.h"

#include "SongState.h"
#include "TrackEvent.h"
#include "Instrument.h"
#include "SampleData.h"

#include "tinyxml2.h"

using namespace tinyxml2;
using namespace std;

SampleData
InstrumentTrack::render(size_t frames, TrackState & state, const std::vector<std::unique_ptr<Instrument> > & instruments, std::map<unsigned int, std::vector<TrackEvent> > & pending_events) {
  assert(getInstrumentId() >= 0 && getInstrumentId() < instruments.size());
  auto & instrument = instruments[getInstrumentId()];

  size_t num_channels = instrument->getNumChannels();
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
	    state.getVoices().stopNote(ev.getId());
	  } else {
	    instrument->playNote(ev.getId(), ev.getFrequency(), ev.getVelocity(), ev.getDelay(), detune, state.getVoices());
	  }
	}
	it = pending_events.erase(it);
      }
      if (it != pending_events.end() && it->first - i < render_size) render_size = it->first - i;
    }     

    state.getVoices().render(data, render_size, i);
    
    i += render_size;
  }

  state.applyEffects(data);

  return data;
}

void
InstrumentTrack::populateXML(XMLElement & element) const {
  Track::populateXML(element);
  
  if (getDetune() != 0) element.SetAttribute("detune", getDetune());
  element.SetAttribute("instrument", getInstrumentId());  
}
