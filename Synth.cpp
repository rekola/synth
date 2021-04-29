#include "Synth.h"

#include "SampleData.h"

#include <cmath>
#include <cassert>

using namespace std;

SampleData
Synth::play(Song & song, size_t frames) {
  SampleData master(2, frames);
  float * out = master.data();

#if 0
  int solo_instrument = -1;
  for (size_t i = 0; i < song.getInstruments().size(); i++) {
    if (song.getInstrument(i).getSolo()) solo_instrument = i;
  }
#endif
  
  if (is_playing) {
    for (size_t i = 0; i < frames; i++) {
      if (samplepos == 0) {
	auto & pattern = song.getPattern(getPatternPosition());
	auto tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : song.getTuning();

	for (size_t col = 0; col < song.getTracks().size(); col++) {
	  auto & track = song.getTrack(col);
	  auto & notes = pattern.getNotes(col, getTrackPosition());
	  for (size_t j = 0; j < notes.size(); j++) {
	    if (notes[j].isDefined()) {
	      auto pos = i + (unsigned int)(song.getRandomizationFactor() * 44100.0f * rand() / RAND_MAX);
	      track.addPendingNote(pos, int(j), tuning, notes[j]);
	    }
	  }
	}
      }

      moveForwardSample(song);
    }
  }

  for (auto & track : song.getTracks()) {
    auto & instrument = song.getInstrument(track.getInstrumentId());
    
    SampleData data(1, frames);
    auto buffer = data.data();
    
    for (size_t i = 0; i < frames; i++) {
      auto & pending = track.getPendingNotes();
      if (!pending.empty()) {
	auto it = pending.begin();
	if (i >= it->first) {
	  for (auto & [ id, tuning, note ] : it->second) {
	    track.playNote(tuning, note, instrument, id);
	  }
	  pending.erase(it);
	}
      }

      float ss = 0;
      for (auto & voice : track.getVoices()) {
	if (voice->isPlaying()) {
	  float adsrvol = voice->updateADSR(instrument.getEnvelope());
	  float s;
	  voice->render(&s, 1);
	  ss += s * track.getVolume() * adsrvol;
	  // if (solo_instrument != -1 && pattern.getInstrumentId() != solo_instrument) ss = 0;	  
	}
      }

      if (ss > 1.0) ss = 1.0;
      else if (ss < -1.0) ss = -1.0;

#if 0
      if (track.hasSample()) {
	auto & sample = track.getSample();
	
      }
#endif

      buffer[i] = ss;
    }

    auto remaining = track.getPendingNotes();
    track.clearPendingNotes();

    for (auto pd : remaining) {
      assert(pd.first >= frames);
      for (auto & [ id, tuning, note ] : pd.second) {       
	track.addPendingNote(pd.first - frames, id, tuning, note);
      }
    }
    
    instrument.applyEffects(data);
    
    for (size_t i = 0; i < frames; i++) {
      float ss = buffer[i];
      
      out[2 * i + 0] += ss * sqrtf(1.0 - track.getPan());
      out[2 * i + 1] += ss * sqrtf(track.getPan());
    }
  }

  for (size_t i = 0; i < frames; i++) {
    auto & left = out[2 * i + 0];
    auto & right = out[2 * i + 1];
    
    left *= song.getMasterVolume();
    right *= song.getMasterVolume();
    
    if (left > 1.0) left = 1.0;
    else if (left < -1.0) left = -1.0;
    if (right > 1.0) right = 1.0;
    else if (right < -1.0) right = -1.0;
  }

  return master;
}
