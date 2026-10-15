#ifndef _ALSAMIDIPORTS_H_
#define _ALSAMIDIPORTS_H_

#include "MidiPorts.h"

#include <alsa/asoundlib.h>

class AlsaMidiPorts : public MidiPortBackend {
 public:
  ~AlsaMidiPorts() override;

  bool open(Logger & logger) override;
  std::vector<MidiPortInfo> ports() override;
  std::optional<MidiPortInfo> port(MidiPortAddress address) override;
  bool connect(MidiPortAddress address) override;
  void disconnect(MidiPortAddress address) override;
  void sendSysEx(MidiPortAddress address, const std::vector<uint8_t> & bytes) override;
  std::vector<MidiMessage> receive() override;
  void addTo(WaitSet & waits, WaitSet::Group group) override;

 private:
  snd_seq_t * seq_handle_ = nullptr;
  int our_port_ = -1;
};

#endif
