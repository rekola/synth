#ifndef _FILEINSTRUMENT_H_
#define _FILEINSTRUMENT_H_

#include "Instrument.h"

#include <string>

class FileInstrument : public Instrument {
 public:
  explicit FileInstrument(std::string filename) : filename_(std::move(filename)) {
    openFile();
  }

  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, float azimuth, float frequency, float detune, float velocity, float start_phase) const override;

protected:
  bool openFile();

private:
  std::string filename_;
  std::shared_ptr<SampleData> samples_;
};

#endif
