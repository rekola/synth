#include "InstrumentTrack.h"

#include "../state/SongState.h"
#include "../state/InstrumentTrackState.h"

using namespace std;

std::unique_ptr<TrackState>
InstrumentTrack::createState(const ChannelConfiguration & config, const SongStructure & structure) const {
  assert(getInstrumentId() >= 0);
  return std::make_unique<InstrumentTrackState>(config, isSolo(), isMuted(), getInternalId(), getInstrumentId(), getPosition(), getSends());
}

void
InstrumentTrack::loadParameters(const ParameterSource & input) {
  LeafTrack::loadParameters(input);

  setInstrumentId(input.get<int>("instrument"));
}

void
InstrumentTrack::storeParameters(ParameterSource & output) const {
  LeafTrack::storeParameters(output);

  output.set("instrument", getInstrumentId());
}
