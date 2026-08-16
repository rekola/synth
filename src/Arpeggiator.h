#ifndef _ARPEGGIATOR_H_
#define _ARPEGGIATOR_H_

#include "InstrumentTrack.h"

// A track kind (like PercussionTrack/DrumMachineTrack - InstrumentTrack.h),
// not an instrument-wrapping node: an <arpeggiatorTrack> plays whatever
// instrument its own (inherited) instrument_id_ points to, exactly like a
// plain <track>, just stepped through a held chord instead of playing it
// directly - see ArpeggiatorState.h and plans/arpeggiator.md.
class Arpeggiator : public InstrumentTrack {
 public:
  enum Mode {
    UP = 1,
    DOWN,
    UP_DOWN
  };
  Arpeggiator() : InstrumentTrack(TrackType::INSTRUMENT_CONTROL) { }

  const char * getElementName() const override { return "arpeggiatorTrack"; }
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const override;
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;

  Mode getMode() const { return mode_; }
  int getNoteDuration() const { return note_duration_; }
  int getOctaves() const { return octaves_; }
  int getGate() const { return gate_; }

 private:
  Mode mode_ = UP;
  int note_duration_ = 1;
  int octaves_ = 0;
  int gate_ = 1; // length of the note
};

#endif
