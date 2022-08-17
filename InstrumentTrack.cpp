#include "InstrumentTrack.h"

#include "SongState.h"
#include "InstrumentTrackState.h"

using namespace std;
  
std::unique_ptr<TrackState>
InstrumentTrack::createState(const ChannelConfiguration & config) const {
  assert(getInstrumentId() >= 0 && getInstrumentId() < instruments.size());
  return std::make_unique<InstrumentTrackState>(config, getId(), getInstrumentId(), getAzimuth(), isSolo());
}

SampleData
InstrumentTrack::render(int frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events) const {
  auto & track_state = song_state.getTrackState(*this);
  return track_state.render(frames, instruments, events);
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
