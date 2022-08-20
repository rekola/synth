#include "InstrumentTrack.h"

#include "SongState.h"
#include "InstrumentTrackState.h"

using namespace std;
  
std::unique_ptr<TrackState>
InstrumentTrack::createState(const ChannelConfiguration & config) const {
  assert(getInstrumentId() >= 0 && getInstrumentId() < instruments.size());
  return std::make_unique<InstrumentTrackState>(config, getInternalId(), getInstrumentId(), getAzimuth(), isSolo(), portamento_);
}

SampleData
InstrumentTrack::render(int frames, const std::vector<std::unique_ptr<Track> > & instruments, RenderContext & context) const {
  auto track_state = context.getTrackState(getInternalId());
  if (!track_state) {
    auto state = createState(context.getChannelConfiguration());
    track_state = state.get();
    context.setTrackState(getInternalId(), std::move(state));
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
  setColor(input.getText("color"));
  portamento_ = input.getFloat("portamento", -1.0f);
}

void
InstrumentTrack::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);
  
  output.set("instrument", getInstrumentId());
  output.set("azimuth", getAzimuth());
  output.set("distance", getDistance());
  output.set("elevation", getElevation());
  output.set("color", getColor());
  if (portamento_ >= 0.0f) output.set("portamento", portamento_);
}
