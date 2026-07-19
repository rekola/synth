#include "Arpeggiator.h"

#include <cassert>

using namespace std;

std::unique_ptr<TrackState>
Arpeggiator::playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, float start_phase, int note_value) const {
  auto group = createState(channel_config);
  for (auto & child : getChildren()) {
    
  }
  return group;
}

void
Arpeggiator::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);
}

void
Arpeggiator::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);
}
