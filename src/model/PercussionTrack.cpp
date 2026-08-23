#include "PercussionTrack.h"

#include "../state/InstrumentTrackState.h"

namespace {

// Runtime counterpart of PercussionTrack, local to this file - nothing
// outside createState() below ever names it (Player.cpp's live-audition
// path and InstrumentTrackState::render() both reach it only through the
// base InstrumentTrackState pointer/reference they already have). Identical
// to InstrumentTrackState in every respect except which instrument its
// notes play through: a PercussionTrack has no instrument_id_/pool index of
// its own (see PercussionTrack.h) - every note plays through the pool's one
// drum kit instead (InstrumentPool::getDefaultKitInstrument()). The base
// constructor's `instrument_id` argument is fixed at -1 here since it's
// never read - getInstrumentSource() below ignores instrument_id_ entirely.
class PercussionTrackState : public InstrumentTrackState {
public:
  explicit PercussionTrackState(const ChannelConfiguration & channel_config, bool solo, bool muted, int track_id, const SphericalPosition & position, const SendLevels & sends)
    : InstrumentTrackState(channel_config, solo, muted, track_id, -1, position, sends) { }

  const Track * getInstrumentSource(const InstrumentPool & instruments) const override {
    return instruments.getDefaultKitInstrument();
  }
};

}

std::unique_ptr<TrackState>
PercussionTrack::createState(const ChannelConfiguration & config, const SongStructure & structure) const {
  return std::make_unique<PercussionTrackState>(config, isSolo(), isMuted(), getInternalId(), getPosition(), getSends());
}
