#include "Arpeggiator.h"
#include "../state/ArpeggiatorState.h"

#include <cassert>

using namespace std;

std::unique_ptr<TrackState>
Arpeggiator::createState(const ChannelConfiguration & config, const SongStructure & structure) const {
  assert(getInstrumentId() >= 0);
  return make_unique<ArpeggiatorState>(config, isSolo(), isMuted(), getInternalId(), getInstrumentId(), getPosition(), getSends(), *this);
}

void
Arpeggiator::loadParameters(const ParameterSource & input) {
  InstrumentTrack::loadParameters(input);

  auto mode_text = input.getText("mode", "up");
  if (mode_text == "down") mode_ = DOWN;
  else if (mode_text == "updown") mode_ = UP_DOWN;
  else mode_ = UP;

  note_duration_ = input.getInt("noteDuration", 1);
  octaves_ = input.getInt("octaves", 0);
  gate_ = input.getInt("gate", 1);
}

void
Arpeggiator::storeParameters(ParameterSource & output) const {
  InstrumentTrack::storeParameters(output);

  output.set("mode", mode_ == DOWN ? "down" : mode_ == UP_DOWN ? "updown" : "up");
  output.set("noteDuration", note_duration_);
  output.set("octaves", octaves_);
  output.set("gate", gate_);
}
