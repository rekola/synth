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
InstrumentTrack::render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) const {
  auto track_state = context.getTrackState(getId());
  if (!track_state) {
    auto state = createState(context.getChannelConfiguration());
    track_state = state.get();
    context.setTrackState(getId(), std::move(state));
  }  
  return track_state->render(frames, instruments, context);
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
