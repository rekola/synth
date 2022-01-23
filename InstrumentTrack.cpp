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
InstrumentTrack::render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events) {
  size_t num_channels = song_state.getChannelConfiguration() == ChannelConfiguration::MONO ? 1 : 2;
  
  SampleData data(num_channels, frames, isSolo());

  assert(getInstrumentId() >= 0 && getInstrumentId() < instruments.size());
  if (getInstrumentId() >= 0 && getInstrumentId() < instruments.size()) {
    auto & instrument = instruments[getInstrumentId()];
    auto & track_voices = song_state.getTrackVoices(getId());
    auto & pending_events = events.getPendingEvents(getId());
  					  
    for (size_t i = 0; i < frames; ) {
      size_t render_size = frames - i;
      if (!pending_events.empty()) {
	auto it = pending_events.begin();
	assert(i <= it->first);
	assert(i == 0 || i == it->first); 
	if (i == it->first) {
	  for (auto & ev : it->second) {
	    if (ev.isAftertouch()) {
	      track_voices.applyAftertouch(ev.getId(), ev.getVelocity());
	    } else {
	      track_voices.stopVoices(ev.getId());
	      if (!ev.isOff()) {
		auto voice = instrument->playNote(song_state.getChannelConfiguration(), song_state.getOutSampleRate(), azimuth, ev.getFrequency(), ev.getVelocity());
		track_voices.addVoice(ev.getId(), move(voice));
	      }
	    }
	  }
	  it = pending_events.erase(it);
	}
	if (it != pending_events.end() && it->first - i < render_size) render_size = it->first - i;
      }     
      
      track_voices.render(data, render_size, i);
      
      i += render_size;
    }
  }
 
  return data;
}

void
InstrumentTrack::readXML(XMLElement & element) {
  Track::readXML(element);

  auto instrument_text = element.Attribute("instrument");
  setInstrumentId(instrument_text ? atoi(instrument_text) : 0);

  auto azimuth_text = element.Attribute("azimuth");
  auto distance_text = element.Attribute("distance");
  auto elevation_text = element.Attribute("elevation");

  setAzimuth(azimuth_text ? strtof(azimuth_text, nullptr) : 0.0f);
  setDistance(distance_text ? strtof(distance_text, nullptr) : 0.0f);
  setElevation(elevation_text ? strtof(elevation_text, nullptr) : 0.0f);  
}

void
InstrumentTrack::populateXML(XMLElement & element) const {
  Track::populateXML(element);
  
  element.SetAttribute("instrument", getInstrumentId());
  
  element.SetAttribute("azimuth", getAzimuth());
  element.SetAttribute("distance", getDistance());
  element.SetAttribute("elevation", getElevation());
}
