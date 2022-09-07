#include "TrackState.h"

#include "Track.h"

TrackState &
TrackState::getChildState(const Track & track) {
  auto it = children_.find(track.getInternalId());
  if (it != children_.end()) return *(it->second);
  auto state = track.createStateTree(channel_config_);
  auto track_state = state.get();
  addChild(track.getInternalId(), std::move(state));
  return *track_state;
}
