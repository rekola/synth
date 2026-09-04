#ifndef _AUDIOAPI_H_
#define _AUDIOAPI_H_

#include <poll.h>
#include <cstddef>
#include <vector>

#include "../playback/MidiEvent.h"

class AudioBuffer;
class Logger;

class AudioAPI {
 public:
  explicit AudioAPI(int _frequency, short _channels) : frequency(_frequency), channels(_channels) { }
  virtual ~AudioAPI() { }
  
  virtual void play(const AudioBuffer & data, Logger & logger) = 0;
  virtual AudioBuffer record(Logger & logger) = 0;
  virtual size_t getFrameCount() const = 0;
  virtual void startRecording() = 0;
  virtual void stopRecording() = 0;
  virtual std::vector<MidiEvent> recordMIDI() = 0;

  // Round-trip recording-latency compensation: frames still sitting queued
  // in the playback ring buffer before what's written next actually
  // reaches the speaker, and frames already captured by the hardware but
  // not yet delivered to the app, respectively - Player.cpp sums both
  // into one latency figure, measured once per take, the instant
  // recording actually engages while the transport is playing. A failed/
  // unavailable measurement fails open to 0 (no compensation - a live
  // take is otherwise unaffected) rather than erroring - a missed
  // measurement should degrade gracefully, not break recording.
  virtual int getPlaybackDelayFrames() const = 0;
  virtual int getCaptureDelayFrames() const = 0;
  
  int getFrequency() const { return frequency; }
  short numberOfChannels() const { return channels; }

  const std::vector<pollfd> getPlaybackDescriptors() const { return playback_descriptors; }
  const std::vector<pollfd> getCaptureDescriptors() const { return capture_descriptors; }
  const std::vector<pollfd> getMidiCaptureDescriptors() const { return midi_capture_descriptors; }
  
protected:
  void setFrequency(int _frequency) { frequency = _frequency; }
  void setPlaybackDescriptors(const std::vector<pollfd> & d) { playback_descriptors = d; }
  void setCaptureDescriptors(const std::vector<pollfd> & d) { capture_descriptors = d; }
  void setMidiCaptureDescriptors(const std::vector<pollfd> & d) { midi_capture_descriptors = d; }
  
private:
  int frequency;
  short channels;
  std::vector<pollfd> playback_descriptors, capture_descriptors, midi_capture_descriptors;
};

#endif
