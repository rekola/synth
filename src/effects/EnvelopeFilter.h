#ifndef _ENVELOPEFILTER_H_
#define _ENVELOPEFILTER_H_

#include "Effect.h"
#include "../instruments/Envelope.h"

class EnvelopeFilter : public Effect {
 public:
  EnvelopeFilter() { }
  
  std::unique_ptr<TrackState> createState(const ChannelConfiguration & channel_config, const SongStructure & structure) const override;
  std::unique_ptr<VoiceState> createVoiceState(const ChannelConfiguration & channel_config) const override;
  const char * getElementName() const override { return "envelope"; }
  // Builds the voice with the note's key number, for keynumToHold/
  // keynumToDecay.
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, SpatialMode spatial_mode, Tuning tuning, float detune,
                                       float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord) const override;

  void loadParameters(const ParameterSource & input) override {
    Effect::loadParameters(input);
    envelope_.loadParameters(input);
  }

  void storeParameters(ParameterSource & output) const override {
    Effect::storeParameters(output);
    envelope_.storeParameters(output);
  }

 private:
  Envelope envelope_;
};

#endif
