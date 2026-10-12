#ifndef _ALSAMIDIINPUT_H_
#define _ALSAMIDIINPUT_H_

#include "MidiInput.h"

#include <alsa/asoundlib.h>

class AlsaMidiInput : public MidiInput {
 public:
  // A machine without the sequencer (no snd-seq module, say) is not an error:
  // there is just no MIDI.
  explicit AlsaMidiInput(Logger & logger);
  ~AlsaMidiInput() override;

  void setSource(const std::string & spec, Logger & logger) override;
  std::vector<MidiEvent> readEvents() override;
  void addTo(WaitSet & waits, WaitSet::Group group) override;

 private:
  // Resolves spec_ against the sequencer's current ports and connects it,
  // dropping the previous connection. Quiet when the source simply isn't
  // there (yet) and no logger is given.
  void connect(Logger * logger);

  snd_seq_t * seq_handle_ = nullptr;
  std::string spec_;
  int connected_client_ = -1, connected_port_ = -1;
  int own_port_ = 0;
};

#endif
