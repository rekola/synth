#include "NoteMultiplier.h"

#include "tinyxml2.h"

#include <cassert>

using namespace std;

std::unique_ptr<TrackState>
NoteMultiplier::playNote(ChannelConfiguration channel_config, unsigned int outSampleRate, float azimuth, float frequency, float velocity, float start_phase) const {
  auto group = createState(channel_config, outSampleRate);
  for (auto & child : getChildren()) {
    float unison_velocity = velocity; // / (unisons + 1);
    
    // root
    auto voice = child->playNote(channel_config, outSampleRate, azimuth, frequency, unison_velocity, start_phase + (float)rand() / RAND_MAX);
    if (voice.get()) group->addChild(move(voice));

    float frequency_start = frequency * (1 - detune / 2);
    float frequency_end = frequency * (1 + detune / 2);

    float azimuth_start = azimuth - spread / 2;
    float azimuth_end = azimuth + spread / 2;

    if (unisons >= 2) {
      float frequency_step = powf(frequency_end / frequency_start, 1.0f / (unisons - 1));
      float azimuth_step = spread / (unisons - 1);
      
      // unisons
      for (int i = 0; i < unisons; i++) {
	auto voice = child->playNote(channel_config, outSampleRate, azimuth_start + i * azimuth_step, frequency_start * powf(frequency_step, i), unison_velocity, start_phase + (float)rand() / RAND_MAX);
	if (voice.get()) group->addChild(move(voice));
      }
    }

    size_t i;
    float m;
    // octaves
    for (i = 0, m = 2; i < octaves; i++, m *= 2) {
      float f = frequency;
      if (detune != 0) f *= (float)rand() / RAND_MAX * (detune - 0.5 * detune);
      auto voice = child->playNote(channel_config, outSampleRate, azimuth, f * m, velocity / m, start_phase + (float)rand() / RAND_MAX);
      if (voice.get()) group->addChild(move(voice));
    }

    // fifths
    for (i = 0, m = 3.0f / 2.0f; i < fifths; i++, m *= 3.0f / 2.0f) {
      float f = frequency;
      if (detune != 0) f *= (float)rand() / RAND_MAX * (detune - 0.5 * detune);
      auto voice = child->playNote(channel_config, outSampleRate, azimuth, f * m, velocity / m, start_phase + (float)rand() / RAND_MAX);
      if (voice.get()) group->addChild(move(voice));
    }

    for (i = 0, m = 4.0f / 3.0f; i < fourths; i++, m *= 4.0f / 3.0f) {
      float f = frequency;
      if (detune != 0) f *= (float)rand() / RAND_MAX * (detune - 0.5 * detune);
      auto voice = child->playNote(channel_config, outSampleRate, azimuth, f * m, velocity / m, start_phase + (float)rand() / RAND_MAX);
      if (voice.get()) group->addChild(move(voice));
    }
}
  return group;
}

void
NoteMultiplier::readXML(tinyxml2::XMLElement & element) {
  Effect::readXML(element);

  auto unisons_text = element.Attribute("unisons");
  auto octaves_text = element.Attribute("octaves");
  auto fifths_text = element.Attribute("fifths");
  auto fourths_text = element.Attribute("fourths");
  auto detune_text = element.Attribute("detune");
  auto spread_text = element.Attribute("spread");

  unisons = unisons_text ? atoi(unisons_text) : 0;
  octaves = octaves_text ? atoi(octaves_text) : 0;
  fifths = fifths_text ? atoi(fifths_text) : 0;
  fourths = fourths_text ? atoi(fourths_text) : 0;
  detune = detune_text ? strtof(detune_text, nullptr) : 0;
  spread = spread_text ? strtof(spread_text, nullptr) : 0;

  if (unisons & 1) unisons--;
  if (octaves & 1) octaves--;
  if (fifths & 1) fifths--;
  if (fourths & 1) fourths--;
}

void
NoteMultiplier::populateXML(tinyxml2::XMLElement & element) const {
  Effect::populateXML(element);  
}
