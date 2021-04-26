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
	for (size_t col = 0; col < song.getTracks().size(); col++) {
	  auto & track = song.getTrack(col);
	  auto & notes = pattern.getNotes(col, getTrackPosition());
	  if (!notes.empty()) {
	    track.addPendingNotes(i, notes);
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
	auto & front = pending.front();
	if (i == front.first) {
	  auto & notes = front.second;
	  for (auto & note : notes) {
	    if (note.isDefined()) {
	      track.playNote(note, instrument);
	    }
	  }
	  pending.pop_front();
	}
      }

      float ss = 0;
      for (auto & state : track.getStates()) {
	float adsrvol = state.updateADSR(instrument.getEnvelope());
	ss += instrument.getSample(state) * state.getVelocity() * track.getVolume() * adsrvol;
	// if (solo_instrument != -1 && pattern.getInstrumentId() != solo_instrument) ss = 0;
	instrument.stepForward(state);
      }

      if (ss > 1.0) ss = 1.0;
      else if (ss < -1.0) ss = -1.0;

      buffer[i] = ss;
    }

    instrument.applyEffects(data);
    track.clearPendingNotes();

    for (size_t i = 0; i < frames; i++) {
      float ss = buffer[i];
      
      out[2 * i + 0] += ss * sqrtf(1.0 - track.getPan());
      out[2 * i + 1] += ss * sqrtf(track.getPan());
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
