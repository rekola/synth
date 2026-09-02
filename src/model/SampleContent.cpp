#include "SampleContent.h"

#include "../state/ParameterSource.h"

void
SampleContent::loadParameters(const ParameterSource & input) {
  setInPoint(input.get<float>("in", 0.0f));
  setOutPoint(input.get<float>("out", 0.0f));
  setOriginalTempo(static_cast<short>(input.get<int>("originalTempo", 0)));
}

void
SampleContent::storeParameters(ParameterSource & output) const {
  output.set("in", getInPoint(), 0.0f);
  output.set("out", getOutPoint(), 0.0f);
  output.set("originalTempo", static_cast<int>(getOriginalTempo()), 0);
}
