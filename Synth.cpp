#include "Synth.h"

#include "SampleData.h"

#include <cmath>
#include <cassert>

#define NOTEDOMAIN (float)1/4
#define VOLGAIN 1.0f
#define ACCENTAMT 1.5f

using namespace std;

SampleData
Synth::play(Song & song, size_t frames) {
  SampleData data(frames);
  float * out = data.data();
    
  int solo_instrument = -1;
  for (size_t i = 0; i < song.getInstruments().size(); i++) {
    if (song.getInstrument(i).getSolo()) solo_instrument = i;
  }

  float tnote = (float)60 / song.bpm * NOTEDOMAIN * 2;
  int sinterval = (int)(tnote * samplerate);
  
  for (int i = 0; i < frames; i++) {
    float left = 0, right = 0;
    
    if (is_playing) {
      if (samplepos == 0) {
	auto & section = song.getSection(trkpos);
	for (auto & sequence : section.getSequences()) {
	  auto & instrument = song.getInstrument(sequence.getInstrumentId());
	  instrument.playNote(sequence.getNote(ptrnpos));	
	}
      }

      if (samplepos + 1 < sinterval || ptrnpos + 1 < PATTLEN || trkpos + 1 < song.getSections().size()) {
	complete_pos++;
	samplepos++;
	
	if (samplepos == sinterval) {
	  samplepos = 0;
	  ptrnpos++;
	  if (ptrnpos >= PATTLEN) {
	    ptrnpos = 0;
	    trkpos++;
	  }
	}
      }
    }
    
    for (auto & instrument : song.getInstruments()) {
      float adsrvol = instrument->updateADSR();

      float ss = instrument->getSample();
      ss = instrument->filtersample(ss);

      ss *= instrument->getVolume() * adsrvol * song.gvol;
      // if (solo_instrument != -1 && pattern.getInstrumentId() != solo_instrument) ss = 0;
      
      if (instrument->hasAccent()) ss *= ACCENTAMT;
      
      if (ss > 1.0) ss = 1.0;
      else if (ss < -1.0) ss = -1.0;

      float ssl = ss * sqrtf(1.0 - instrument->getPan());
      float ssr = ss * sqrtf(instrument->getPan());

      if (instrument->getFlags() & DELAYTRACK) instrument->delaysample(song.delaymix1, song.delaymix2, song.fd1, song.delay1, song.fd2, song.delay2, &ssl, &ssr);

      left += ssl;
      right += ssr;

      instrument->stepForward();
    }

    left *= song.mastervol * VOLGAIN;
    right *= song.mastervol * VOLGAIN;

    if (left > 1.0) left = 1.0;
    else if (left < -1.0) left = -1.0;
    if (right > 1.0) right = 1.0;
    else if (right < -1.0) right = -1.0;

    out[2 * i] = left;
    out[2 * i + 1] = right;
  }

  return data;
}
