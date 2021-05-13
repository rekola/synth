#ifndef _AUDIOAPI_H_
#define _AUDIOAPI_H_

#include <poll.h>
#include <vector>

class SampleData;
class Logger;

class AudioAPI {
 public:
  explicit AudioAPI(unsigned int _frequency, unsigned short _channels) : frequency(_frequency), channels(_channels) { }
  virtual ~AudioAPI() { }
  
  virtual void play(SampleData & data, Logger & logger) = 0;
  virtual SampleData record(Logger & logger) = 0;
  virtual size_t getFrameCount() const = 0;
  virtual void startRecording() = 0;
  virtual void stopRecording() = 0;
  
  unsigned int getFrequency() const { return frequency; }
  unsigned short getChannels() const { return channels; }

  const std::vector<pollfd> getPlaybackDescriptors() const { return playback_descriptors; }
  const std::vector<pollfd> getCaptureDescriptors() const { return capture_descriptors; }
  
protected:
  void setFrequency(int _frequency) { frequency = _frequency; }
  void setPlaybackDescriptors(const std::vector<pollfd> & d) { playback_descriptors = d; }
  void setCaptureDescriptors(const std::vector<pollfd> & d) { capture_descriptors = d; }
  
private:
  unsigned int frequency;
  unsigned short channels;
  std::vector<pollfd> playback_descriptors, capture_descriptors;
};

#endif
