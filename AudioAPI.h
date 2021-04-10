#ifndef _AUDIOAPI_H_
#define _AUDIOAPI_H_

#include <poll.h>
#include <vector>

class SampleData;
class UI;

class AudioAPI {
 public:
  explicit AudioAPI(unsigned int _frequency, unsigned short _channels) : frequency(_frequency), channels(_channels) { }
  virtual ~AudioAPI() { }
  
  virtual void play(SampleData & data, UI & ui) = 0;
  virtual size_t getFrameCount() const = 0;
  
  unsigned int getFrequency() const { return frequency; }
  unsigned short getChannels() const { return channels; }

  const std::vector<pollfd> getPollDescriptors() const { return descriptors; }
  
protected:
  void setFrequency(int _frequency) { frequency = _frequency; }
  void addPollDescriptor(const pollfd & d) { descriptors.push_back(d); }
  
private:
  unsigned int frequency;
  unsigned short channels;
  std::vector<pollfd> descriptors;
};

#endif
