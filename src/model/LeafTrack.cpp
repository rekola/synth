#include "LeafTrack.h"

void
LeafTrack::loadParameters(const ParameterSource & input) {
  Track::loadParameters(input);

  setSolo(input.get<bool>("solo"));
  setMuted(input.get<bool>("mute"));
  auto monitor = input.get<std::string>("monitor", "auto");
  setMonitor(monitor == "in" ? Monitor::IN : monitor == "off" ? Monitor::OFF : Monitor::AUTO);
  setAzimuth(input.get<float>("azimuth"));
  setDistance(input.get<float>("distance"));
  setElevation(input.get<float>("elevation"));
  setExtent(input.get<float>("extent", -1.0f));
  setSpatialMode(spatialModeFromName(input.get<std::string>("spatial", "auto")));
  setMinNoteColumns(input.get<int>("noteColumns", 1));
}

void
LeafTrack::storeParameters(ParameterSource & output) const {
  Track::storeParameters(output);

  output.set("azimuth", getAzimuth());
  output.set("distance", getDistance());
  output.set("elevation", getElevation());
  output.set("extent", getExtent(), -1.0f);
  if (getSpatialMode() != SpatialMode::AUTO) output.set("spatial", std::string(spatialModeName(getSpatialMode())));
  if (isSolo()) output.set("solo", true);
  if (isMuted()) output.set("mute", true);
  if (getMonitor() != Monitor::AUTO) output.set("monitor", std::string(getMonitor() == Monitor::IN ? "in" : "off"));
  if (min_note_columns_ != 1) output.set("noteColumns", min_note_columns_);
}
