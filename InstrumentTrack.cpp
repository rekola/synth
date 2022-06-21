#include "InstrumentTrack.h"

#include "SongState.h"
#include "TrackEvent.h"
#include "Instrument.h"
#include "SampleData.h"
#include "TrackEventQueue.h"

using namespace std;

SampleData
InstrumentTrack::render(int frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events) {
  SampleData data(song_state.getChannelConfiguration(), 0, isSolo());

  assert(getInstrumentId() >= 0 && getInstrumentId() < instruments.size());
  if (getInstrumentId() >= 0 && getInstrumentId() < instruments.size()) {
    auto & instrument = instruments[getInstrumentId()];
    auto & track_state = song_state.getTrackState(*this);
    auto & pending_events = events.getPendingEvents(getId());
  					  
    for (int i = 0; i < frames; ) {
      int render_size = frames - i;
      if (!pending_events.empty()) {
	auto it = pending_events.begin();
	assert(i <= it->first);
	assert(i == 0 || i == it->first); 
	if (i == it->first) {
	  for (auto & ev : it->second) {
	    if (ev.isAftertouch()) {
	      track_state.applyAftertouch(ev.getId(), ev.getVelocity());
	    } else {
	      track_state.stopVoices(ev.getId());
	      if (!ev.isOff()) {
		auto voice = instrument->playNote(song_state.getChannelConfiguration(), azimuth, ev.getFrequency(), ev.getVelocity());
		track_state.addVoice(ev.getId(), move(voice));
	      }
	    }
	  }
	  it = pending_events.erase(it);
	}
	if (it != pending_events.end() && it->first - i < render_size) render_size = it->first - i;
      }     
      
      data.append(track_state.render(render_size));
      
      i += render_size;
    }
  }
 
  return data;
}

void
InstrumentTrack::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);

  setInstrumentId(input.getInt("instrument"));
  setAzimuth(input.getFloat("azimuth"));
  setDistance(input.getFloat("distance"));
  setElevation(input.getFloat("elevation"));
}

void
InstrumentTrack::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);
  
  output.set("instrument", getInstrumentId());
  output.set("azimuth", getAzimuth());
  output.set("distance", getDistance());
  output.set("elevation", getElevation());
}
