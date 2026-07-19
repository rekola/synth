#include "InstrumentTrack.h"

#include "SongState.h"
#include "InstrumentTrackState.h"

using namespace std;
  
std::unique_ptr<TrackState>
InstrumentTrack::createState(const ChannelConfiguration & config) const {
  assert(getInstrumentId() >= 0);
  return std::make_unique<InstrumentTrackState>(config, isSolo(), isMuted(), getInternalId(), getInstrumentId(), getPosition(), portamento_);
}

void
InstrumentTrack::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
  
  setInstrumentId(input.getInt("instrument"));
  setSolo(input.getBool("solo"));
  setMuted(input.getBool("mute"));
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
  if (isSolo()) output.set("solo", true);
  if (isMuted()) output.set("mute", true);
  output.set("azimuth", getAzimuth());
  output.set("distance", getDistance());
  output.set("elevation", getElevation());
  output.set("color", getColor());
  if (portamento_ >= 0.0f) output.set("portamento", portamento_);
}
