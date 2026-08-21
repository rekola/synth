#include "DrumMachineTrack.h"

#include "../state/DrumMachineTrackState.h"
#include "../instruments/DrumRankTable.h"

#include <algorithm>

using namespace std;

void
DrumMachineTrack::seedDefaultKit() {
  // The default rock kit: kick (36), snare (38), low/low-mid/high tom
  // (45/47/50), closed/open hi-hat (42/46), crash (49) - addLane() derives
  // lane order via DrumRankTable itself, so this list needn't be
  // pre-sorted. All-rest until the player programs it. addLane() is
  // itself a safe no-op for any note that already has a lane, so calling
  // this on a track that isn't freshly-constructed just fills in whatever
  // default lanes are still missing rather than duplicating existing ones.
  for (int note : { 36, 38, 45, 47, 50, 42, 46, 49 }) addLane(note);
}

unique_ptr<TrackState>
DrumMachineTrack::createState(const ChannelConfiguration & config, const SongStructure & structure) const {
  assert(getInstrumentId() >= 0);
  return make_unique<DrumMachineTrackState>(config, isSolo(), isMuted(), getInternalId(), getInstrumentId(), getPosition(), getSends());
}

bool
DrumMachineTrack::hasLane(int note) const {
  return find(lane_notes_.begin(), lane_notes_.end(), note) != lane_notes_.end();
}

void
DrumMachineTrack::addLane(int note) {
  if (hasLane(note)) return;
  if (static_cast<int>(lane_notes_.size()) >= kMaxLanes) return;
  lane_notes_.push_back(note);
  lane_notes_ = DrumRankTable::orderLanes(move(lane_notes_));
  steps_[note] = 0;
}

void
DrumMachineTrack::removeLane(int note) {
  auto it = find(lane_notes_.begin(), lane_notes_.end(), note);
  if (it == lane_notes_.end()) return;
  lane_notes_.erase(it);
  steps_.erase(note);
}

void
DrumMachineTrack::clearAllLanes() {
  for (int note : vector<int>(lane_notes_)) removeLane(note); // copy: removeLane mutates lane_notes_
}

uint8_t
DrumMachineTrack::getSteps(int note) const {
  auto it = steps_.find(note);
  return it == steps_.end() ? 0 : it->second;
}

void
DrumMachineTrack::setSteps(int note, uint8_t steps) {
  if (!hasLane(note)) return;
  steps_[note] = steps;
}

void
DrumMachineTrack::setStep(int note, int step, bool hit) {
  if (!hasLane(note)) return;
  auto bit = static_cast<uint8_t>(1u << step);
  if (hit) steps_[note] = static_cast<uint8_t>(steps_[note] | bit);
  else steps_[note] = static_cast<uint8_t>(steps_[note] & ~bit);
}

vector<int>
DrumMachineTrack::getHitNotesForRow(int pattern_row) const {
  vector<int> hits;
  if (loop_length_ <= 0 || pattern_row < 0) return hits;
  auto source_row = pattern_row % loop_length_;
  for (int note : lane_notes_) {
    if ((getSteps(note) & (1u << source_row)) != 0) hits.push_back(note);
  }
  return hits;
}
