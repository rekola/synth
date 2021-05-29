#ifndef _INSTRUMENT_H_
#define _INSTRUMENT_H_

#include "Track.h"
#include "Effect.h"
#include "Envelope.h"
#include "VoicePool.h"

#include <string>
#include <vector>
#include <memory>

class InstrumentVoice;

namespace tinyxml2 {
  class XMLDocument;
  class XMLElement;
};

class Instrument : public Track {
public:
  explicit Instrument(size_t _num_channels) : Track(INSTRUMENT), num_channels(_num_channels) { }
  explicit Instrument(size_t _num_channels, std::string _name) : Track(INSTRUMENT, _name), num_channels(_num_channels) { }

  virtual std::unique_ptr<InstrumentVoice> createVoice(unsigned int outSampleRate, int _identifier) const = 0;

  virtual void playNote(size_t column, float frequency, float velocity, float delay, float detune, VoicePool & voices) const {
    voices.stopVoices(column);
    getVoice(column, voices).playNote(frequency, velocity, delay, detune);
  }
  
  virtual tinyxml2::XMLElement * createXML(tinyxml2::XMLDocument & doc) const { return 0; }

  size_t getNumChannels() const { return num_channels; }
    
  void setAmpEnvelope(const Envelope & _amp_envelope) { amp_envelope = _amp_envelope; }  
  const Envelope & getAmpEnvelope() const { return amp_envelope; }
  const Envelope & getModEnvelope() const { return mod_envelope; }
  
  void addEffect(std::unique_ptr<Effect> effect) { effects.push_back(std::move(effect)); }
  const std::vector<std::unique_ptr<Effect> > & getEffects() const { return effects; }
  
  float getGain() const { return gain; }

protected:
  InstrumentVoice & getVoice(size_t column, VoicePool & voices) const {
    for (auto & voice : voices.getVoices()) {
      if (!voice->isPlaying()) {
	voice->setIdentifier(column);
	return *voice;	
      }
    }
    return voices.addVoice(createVoice(voices.getOutSampleRate(), column));
  }

  size_t num_channels;
  Envelope amp_envelope, mod_envelope;
  float gain = 1.0f;

  std::vector<std::unique_ptr<Effect> > effects;
};

#endif
