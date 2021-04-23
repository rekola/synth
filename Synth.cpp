#include "Synth.h"

#include "SampleData.h"

#include <cmath>
#include <cassert>

#define ACCENTAMT 1.5f

using namespace std;

SampleData
Synth::play(Song & song, size_t frames) {
  SampleData master(2, frames);
  float * out = master.data();
    
  int solo_instrument = -1;
  for (size_t i = 0; i < song.getInstruments().size(); i++) {
    if (song.getInstrument(i).getSolo()) solo_instrument = i;
  }
  
  if (is_playing) {
    for (size_t i = 0; i < frames; i++) {
      if (samplepos == 0) {
	auto & section = song.getSection(getSectionPosition());
	for (auto & sequence : section.getSequences()) {
	  auto & instrument = song.getInstrument(sequence.getInstrumentId());
	  instrument.addPendingNote(i, sequence.getNote(getSequencePosition()));
	}
      }

      moveForwardSample(song);
    }
  }
  
  for (auto & instrument : song.getInstruments()) {
    SampleData data(1, frames);
    auto buffer = data.data();
    
    for (size_t i = 0; i < frames; i++) {
      auto & pending = instrument->getPendingNotes();
      if (!pending.empty()) {
	auto & front = pending.front();
	if (i == front.first) {
	  instrument->playNote(front.second);
	  pending.pop_front();
	}
      }
      
      float adsrvol = instrument->updateADSR();      
      // ss = instrument->filtersample(ss);
      float ss = instrument->getSample() * instrument->getVolume() * adsrvol * song.gvol;
      if (instrument->hasAccent()) ss *= ACCENTAMT;

      if (ss > 1.0) ss = 1.0;
      else if (ss < -1.0) ss = -1.0;

      buffer[i] = ss;
      // if (solo_instrument != -1 && pattern.getInstrumentId() != solo_instrument) ss = 0;
      instrument->stepForward();
    }

    instrument->applyEffects(data);
    instrument->clearPendingNotes();

    for (size_t i = 0; i < frames; i++) {
      float ss = buffer[i];
      
      out[2 * i + 0] += ss * sqrtf(1.0 - instrument->getPan());
      out[2 * i + 1] += ss * sqrtf(instrument->getPan());
    }
  }

  for (size_t i = 0; i < frames; i++) {
    auto & left = out[2 * i + 0];
    auto & right = out[2 * i + 1];
    
    left *= song.mastervol;
    right *= song.mastervol;
    
    if (left > 1.0) left = 1.0;
    else if (left < -1.0) left = -1.0;
    if (right > 1.0) right = 1.0;
    else if (right < -1.0) right = -1.0;
  }

  return master;
}
