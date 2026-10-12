#ifndef _AUDIOAPI_H_
#define _AUDIOAPI_H_

#include <cstddef>
#include <string>
#include <vector>

#include "../util/Notifier.h"

class AudioBuffer;
class Logger;

// What AudioAPI::wait() found ready; all false means it timed out.
struct WaitResult {
  bool woken = false;    // the notifier was raised
  bool playback = false; // room for another block of output
  bool capture = false;  // a block of input is ready (only if asked for)
};

class AudioAPI {
 public:
  explicit AudioAPI(int _frequency, short _channels) : frequency(_frequency), channels(_channels) { }
  virtual ~AudioAPI() { }
  
  // Blocks until `wake` is raised, the device wants another block of output,
  // captured input is ready (watched only while `want_capture`) or timeout_ms
  // passes. The backend owns whatever it waits on, so it follows a device
  // switch without the caller knowing; wake is not cleared.
  virtual WaitResult wait(Notifier & wake, bool want_capture, int timeout_ms) = 0;

  virtual void play(const AudioBuffer & data, Logger & logger) = 0;
  virtual AudioBuffer record(Logger & logger) = 0;
  virtual size_t getFrameCount() const = 0;
  virtual void startRecording() = 0;
  virtual void stopRecording() = 0;

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

  // Runtime device switching. Both are called on the audio thread (Player
  // handles them as PlaybackControlEvents) because it owns the PCM handles.
  // A device that can't be opened, or can't run at the rate and block size
  // already in use, leaves the current one untouched and returns false after
  // logging why - a failed switch never silences the player. `name` is a
  // DeviceSettings audio name.
  virtual bool setCaptureDevice(const std::string & name, Logger & logger) = 0;
  virtual bool setPlaybackDevice(const std::string & name, Logger & logger) = 0;
  // Like getCaptureDeviceName(): what playback actually opened.
  virtual std::string getPlaybackDeviceName() const = 0;

  int getFrequency() const { return frequency; }
  short numberOfChannels() const { return channels; }

protected:
  void setFrequency(int _frequency) { frequency = _frequency; }

private:
  int frequency;
  short channels;
};

#endif
