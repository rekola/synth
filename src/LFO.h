#ifndef _LFO_H_
#define _LFO_H_

#include "Instrument.h"
#include "SphericalPosition.h"
#include "SendLevels.h"
#include "NoteCoordinate.h"

class LFO : public Instrument {
 public:
  LFO() { }

  const char * getElementName() const override { return "LFO"; }
  void loadParameters(const ParameterSource & input) override;
  void storeParameters(ParameterSource & output) const override;
  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}) const override;

private:
  float frequency_ = 1.0f;
  float level_ = 1.0f;
};

#endif
