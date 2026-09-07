#ifndef _INSTRUMENTTRACK_H_
#define _INSTRUMENTTRACK_H_

#include "LeafTrack.h"

// The one LeafTrack that resolves its sound from the song's own instrument
// pool (InstrumentPool::getInstruments()) by plain index - see LeafTrack.h
// for everything else (position/solo/mute/sends/note columns) every leaf
// track type shares. PercussionTrack deliberately doesn't inherit this - it
// sources its sound from the pool's default kit instead
// (InstrumentPool::getDefaultKitInstrument()), not a per-track pool pick.
class InstrumentTrack : public LeafTrack {
 public:
  InstrumentTrack() : LeafTrack(TrackType::INSTRUMENT_CONTROL), instrument_id_(0) { }
  InstrumentTrack(int instrument_id) : LeafTrack(TrackType::INSTRUMENT_CONTROL), instrument_id_(instrument_id) { }
  InstrumentTrack(TrackType type) : LeafTrack(type), instrument_id_(0) { }

  const char * getElementName() const override { return "track"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const override;

  void loadParameters(const ParameterSource & input);
  void storeParameters(ParameterSource & output) const override;

  int getInstrumentId() const { return instrument_id_; }
  void setInstrumentId(int id) { instrument_id_ = id; }

private:
  int instrument_id_ = 0;
};

#endif
