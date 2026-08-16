#include "BusEffect.h"
#include "../ParameterSource.h"

void
BusEffect::loadParameters(const ParameterSource & input) {
  SongObject::loadParameters(input);
  setWetLevel(input.getFloat("wet", default_wet_level_));
  setChainSendLevel(input.getFloat("chainSend", default_chain_send_level_));
}

void
BusEffect::storeParameters(ParameterSource & output) const {
  SongObject::storeParameters(output);
  output.set("wet", wet_level_, default_wet_level_);
  output.set("chainSend", chain_send_level_, default_chain_send_level_);
}

void
BusEffect::getChainSendSum(float * out, int frames) const {
  for (int i = 0; i < frames; i++) out[i] = 0.0f;

  int n = getNumTaps();
  for (int t = 0; t < n; t++) {
    auto tap = getTap(t);
    for (int i = 0; i < frames; i++) out[i] += tap[i];
  }
}

AmbisonicVoiceEncoder &
BusEffect::getTapEncoder(int i) {
  if (static_cast<int>(tap_encoders_.size()) != getNumTaps()) {
    tap_encoders_.resize(static_cast<size_t>(getNumTaps()));
  }
  return tap_encoders_[static_cast<size_t>(i)];
}
