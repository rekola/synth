#include "InstrumentTrack.h"

#include "SongState.h"
#include "TrackEvent.h"
#include "Instrument.h"
#include "SampleData.h"
#include "TrackEventQueue.h"

#include "tinyxml2.h"

using namespace tinyxml2;
using namespace std;

SampleData
InstrumentTrack::render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Instrument> > & instruments, TrackEventQueue & events) {
  assert(getInstrumentId() >= 0 && getInstrumentId() < instruments.size());
  auto & instrument = instruments[getInstrumentId()];

  auto & track_state = song_state.getTrackState(getId());
  if (!track_state.isInitialized()) {
    // track_state.initialize(track->getEffects());
  }

  size_t num_channels = instrument->getNumChannels();
  assert(num_channels == 1);

  auto & pending_events = events.getPendingEvents(getId());
  
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
	    track_state.getVoices().stopNote(ev.getId());
	  } else {
	    instrument->playNote(ev.getId(), ev.getFrequency(), ev.getVelocity(), ev.getDelay(), detune, track_state.getVoices());
	  }
	}
	it = pending_events.erase(it);
      }
      if (it != pending_events.end() && it->first - i < render_size) render_size = it->first - i;
    }     

    track_state.getVoices().render(data, render_size, i);
    
    i += render_size;
  }

  track_state.applyEffects(data);

  return data;
}

void
InstrumentTrack::readXML(XMLElement & element) {
  Track::readXML(element);

  auto instrument_text = element.Attribute("instrument");
  auto detune_text = element.Attribute("detune");

  setInstrumentId(instrument_text ? atoi(instrument_text) : 0);
  setDetune(detune_text ? atof(detune_text) : 0.0f);
}

void
InstrumentTrack::populateXML(XMLElement & element) const {
  Track::populateXML(element);
  
  if (getDetune() != 0) element.SetAttribute("detune", getDetune());
  element.SetAttribute("instrument", getInstrumentId());  
}
