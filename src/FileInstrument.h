#ifndef _FILEINSTRUMENT_H_
#define _FILEINSTRUMENT_H_

#include "Instrument.h"
#include "SphericalPosition.h"
#include "SendLevels.h"
#include "NoteCoordinate.h"

#include <string>

class FileInstrument : public Instrument {
 public:
  explicit FileInstrument(std::string filename) : filename_(std::move(filename)) {
    openFile();
  }

  std::unique_ptr<VoiceState> playNote(const ChannelConfiguration & channel_config, const SphericalPosition & position, float frequency, float detune, float velocity, int note_value, const SendLevels & sends, const NoteCoordinate & note_coord = {}) const override;

protected:
  bool openFile();

private:
  std::string filename_;
  std::shared_ptr<AudioBuffer> samples_;
};

#endif
