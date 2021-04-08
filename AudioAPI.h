#ifndef _AUDIOAPI_H_
#define _AUDIOAPI_H_

class Synth;

class AudioAPI {
 public:
 AudioAPI(int _frequency, int _channels) : frequency(_frequency), channels(_channels) { }
  virtual ~AudioAPI() { }
  
  virtual void start(Synth & synth) = 0;
  
  int getFrequency() const { return frequency; }
  int getChannels() const { return channels; }

protected:
  void setFrequency(int _frequency) { frequency = _frequency; }
  
private:
  int frequency, channels;
};

#endif
