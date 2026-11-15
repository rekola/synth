#include "TestFramework.h"

#include "../src/state/TrackState.h"

#include <memory>
#include <unordered_map>

namespace {

// A track state whose reading a test sets, as a block's render would.
struct ProbeTrack : TrackState {
  explicit ProbeTrack(const ChannelConfiguration & config) : TrackState(config) {}
  using TrackState::setTrackInfo;
};

} // namespace

// A clip is one block's, and a snapshot is taken every several blocks: the snapshot reports a clip since the last
// one, not the last block's, so the UI sees every clip and a lamp can hold it.
TEST(track_state_snapshot_reports_a_clip_since_the_last_snapshot) {
  ChannelConfiguration config;
  ProbeTrack parent(config);
  auto child = std::make_unique<ProbeTrack>(config);
  ProbeTrack * track = child.get();
  parent.addChild(7, std::move(child));

  std::unordered_map<int, TrackInfo> info;
  track->setTrackInfo(TrackInfo(true, false));
  parent.getAllTrackInfo(info);
  CHECK(info[7].isActive() && !info[7].isClipping());

  track->setTrackInfo(TrackInfo(true, true));  // a block clips
  track->setTrackInfo(TrackInfo(true, false)); // the next blocks do not
  track->setTrackInfo(TrackInfo(true, false));
  parent.getAllTrackInfo(info);
  CHECK(info[7].isClipping()); // the snapshot says it did
  CHECK(info[7].isActive());   // the rest of the reading is the latest

  parent.getAllTrackInfo(info); // reported once: nothing has clipped since
  CHECK(!info[7].isClipping());
}

TEST(track_state_snapshot_keeps_each_tracks_clips_apart) {
  ChannelConfiguration config;
  ProbeTrack parent(config);
  auto a = std::make_unique<ProbeTrack>(config), b = std::make_unique<ProbeTrack>(config);
  ProbeTrack *ta = a.get(), *tb = b.get();
  parent.addChild(1, std::move(a));
  parent.addChild(2, std::move(b));
  ta->setTrackInfo(TrackInfo(true, true));
  tb->setTrackInfo(TrackInfo(true, false));
  std::unordered_map<int, TrackInfo> info;
  parent.getAllTrackInfo(info);
  CHECK(info[1].isClipping() && !info[2].isClipping());
}
