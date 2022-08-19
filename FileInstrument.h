#ifndef _FILEINSTRUMENT_H_
#define _FILEINSTRUMENT_H_

#include "Instrument.h"

#include <string>
#include <vector>

class FileInstrument : public Instrument {
 public:
  explicit FileInstrument(const std::string & _filename) : filename(_filename) {
    openFile();
  }

  std::unique_ptr<TrackState> playNote(const ChannelConfiguration & channel_config, float azimuth, float frequency, float detune, float velocity, float start_phase) const override;

protected:
  void openFile();

private:
  std::string filename;
  std::shared_ptr<SampleData> samples;
};

#endif
