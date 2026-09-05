#ifndef _AUDIOAPI_H_
#define _AUDIOAPI_H_

#include <poll.h>
#include <cstddef>
#include <string>
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

  // Whether a capture device was actually opened and configured at
  // initialize() time - false means recording is silently unavailable
  // (see AlsaAudio::initialize()'s own comment on why a missing/failed
  // capture device doesn't stop playback). Lets the running app tell the
  // user recording isn't available at all, rather than leaving them
  // guessing why nothing ever gets captured.
  virtual bool hasCaptureDevice() const = 0;
  // The device name actually opened for capture ("default" unless
  // overridden - see main.cpp's own --capture-device flag), or empty if
  // hasCaptureDevice() is false.
  virtual std::string getCaptureDeviceName() const = 0;

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
