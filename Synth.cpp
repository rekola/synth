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

  auto & mastertrack = song.getMasterTrack();
  auto & tracks = mastertrack.getChildren();
  
  if (is_playing) {
    for (size_t i = 0; i < frames; i++) {
      if (samplepos == 0) {
	auto & pattern = song.getPattern(getPatternPosition());
	auto tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : song.getTuning();

	for (size_t col = 0; col < tracks.size(); col++) {
	  auto & track = tracks[col];
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

  for (auto & track : tracks) {
    auto & instrument = song.getInstrument(track.getInstrumentId());

    size_t num_channels = instrument.getNumChannels();
    
    SampleData data(num_channels, frames);
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
      
      float track_data[2] = { 0, 0 };
      for (auto & voice : track.getVoices()) {
	if (voice->isPlaying()) {
	  float adsrvol = voice->updateADSR(instrument.getEnvelope());
	  float s[2];
	  voice->render(s, 1);
	  track_data[0] += s[0] * adsrvol;
	  track_data[1] += s[1] * adsrvol;
	  // if (solo_instrument != -1 && pattern.getInstrumentId() != solo_instrument) ss = 0;	  
	}
      }

#if 0
      if (track.hasSample()) {
	auto & sample = track.getSample();
      }
#endif

      if (num_channels == 1) {
	float ss = track_data[0] * track.getVolume();
	
	if (ss > 1.0) ss = 1.0;
	else if (ss < -1.0) ss = -1.0;

	buffer[i] = ss;
      } else {
	float left = track_data[0] * track.getVolume();
	float right = track_data[1] * track.getVolume();

	if (left > 1.0) left = 1.0;
	else if (left < -1.0) left = -1.0;

	if (right > 1.0) right = 1.0;
	else if (right < -1.0) right = -1.0;
	
	buffer[2 * i + 0] = left;
	buffer[2 * i + 1] = right;
      }
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
    track.applyEffects(data);

    float pan = track.getPan();
    
    if (num_channels == 1) {
      for (size_t i = 0; i < frames; i++) {
	float ss = buffer[i];
	
	out[2 * i + 0] += ss * sqrtf(1.0 - pan);
	out[2 * i + 1] += ss * sqrtf(pan);
      }
    } else {
      float left_f = cos(pan * M_PI / 2), right_f = sin(pan * M_PI / 2);
      for (size_t i = 0; i < frames; i++) {
	float left = buffer[2 * i + 0], right = buffer[2 * i + 1];

	out[2 * i + 0] += left_f * left;
	out[2 * i + 1] += right_f * right; 
      }
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
