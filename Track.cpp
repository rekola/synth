#include "Track.h"

#include "SongState.h"

std::atomic<int> Track::next_id(1000);

void
Track::loadParameters(const ParameterSource & input) {
  setId(input.getInt("id", -1));
  setName(input.getText("name"));
  setVolume(input.getFloat("volume", 1.0f));
  setSolo(input.getBool("solo"));
  setMute(input.getBool("mute"));
}

void
Track::storeParameters(ParameterSource & output) const {
  if (!getName().empty()) output.set("name", getName());
  if (isSolo()) output.set("solo", true);
  if (isMuted()) output.set("mute", true);
  output.set("volume", getVolume());
}

SampleData
Track::render(size_t frames, SongState & song_state, const std::vector<std::unique_ptr<Track> > & instruments, TrackEventQueue & events) {
  bool child_has_solo = false;
  for (auto & child : getChildren()) {
    if (child->isSolo()) {
      child_has_solo = true;
      break;
    }
  }

  SampleData sd(song_state.getChannelConfiguration(), frames, isSolo() || child_has_solo);
     	   
  for (auto & child : getChildren()) {
    auto sd2 = child->render(frames, song_state, instruments, events);
    if (!child->isMuted() && (!child_has_solo || child->isSolo())) {
      sd.mix(sd2, child->getVolume());
    }
  }
  return sd;
}
