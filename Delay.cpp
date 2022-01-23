#include "Delay.h"

#include "TrackState.h"

#include "tinyxml2.h"

#define MAX_DELAY_SAMPLES 44100 * 5

using namespace std;

class DelayState : public TrackState {
public:
  DelayState(ChannelConfiguration _channel_config, unsigned int _outSampleRate, int _delay, float _fd, float _delaymix)
    : TrackState(_channel_config, _outSampleRate), delay(_delay), fd(_fd), delaymix(_delaymix) {
    memset(delaybuf, 0, MAX_DELAY_SAMPLES * sizeof(float));
  }
 
  void apply(SampleData & input_data) override {
    assert(input_data.getChannels() == 1);
    float * buffer = input_data.data();
    
    for (size_t i = 0; i < input_data.size(); i++) {
      float x = buffer[i];
      float y = delaybuf[delc];
    
      delaybuf[delc++] = x + y * fd;
      if (delc >= delay) delc = 0;
    
      buffer[i] += delaymix * y;
    }    
  }

private:
  int delay;
  float fd, delaymix;

  // delay state
  int delc = 0;
  float delaybuf[MAX_DELAY_SAMPLES];
};

std::unique_ptr<TrackState>
Delay::createState(ChannelConfiguration channel_config, unsigned int outSampleRate) const {
  return make_unique<DelayState>(channel_config, outSampleRate, delay, fd, delaymix);
}

void
Delay::readXML(tinyxml2::XMLElement & element) {
  Effect::readXML(element);
  
  auto delay_text = element.Attribute("delay");
  if (delay_text) delay = atoi(delay_text);

  auto fd_text = element.Attribute("fd");
  if (fd_text) fd = strtof(fd_text, nullptr);

  auto delaymix_text = element.Attribute("mix");
  if (delaymix_text) delaymix = strtof(delaymix_text, nullptr);
}

void
Delay::populateXML(tinyxml2::XMLElement & element) const {
  Effect::populateXML(element);

  element.SetAttribute("delay", delay);
  element.SetAttribute("fd", fd);
  element.SetAttribute("mix", delaymix);
}
