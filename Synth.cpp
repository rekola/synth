#include "Synth.h"

#include "SampleData.h"
#include "Tuner.h"

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

  Tuner tuner;

  hrft.reset();

#if 0
  size_t tick_frames = getTickInterval(song);
  if (tick_frames > frames) tick_frames = frames;
#endif

  auto sinterval = getSampleInterval(song);

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
	      auto pos = i + (unsigned int)(song.getRandomizationFactor() * samplerate * rand() / RAND_MAX);
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
    assert(num_channels == 1);
    
    SampleData data(num_channels, frames);
    auto buffer = data.data();
    
    for (size_t i = 0; i < frames; ) {     
      size_t render_size = frames - i;
      auto & pending = track.getPendingNotes();
      if (!pending.empty()) {
	auto it = pending.begin();
	if (i >= it->first) {
	  for (auto & [ id, tuning, note ] : it->second) {
	    if (note.isOff()) {
	      track.stopNote(id);
	    } else {
	      auto & pattern = song.getPattern(getPatternPosition());
	      int key = pattern.getKey() >= 0 ? pattern.getKey() : song.getKey();
	      float frequency = tuner.getFrequency(tuning, key, note, instrument.getTranspose());
	      track.playNote(frequency, note.getVelocityAsFloat(), instrument, id);
	    }
	  }
	  it = pending.erase(it);
	}
	if (it != pending.end() && it->first - i < render_size) render_size = it->first - i;
      }     
      
      for (auto & voice : track.getVoices()) {
	if (voice->isPlaying()) {
	  voice->render(buffer, render_size, i);
	  // if (solo_instrument != -1 && pattern.getInstrumentId() != solo_instrument) ss = 0;	  
	}
      }

      i += render_size;
    }

    auto remaining = track.getPendingNotes();
    track.clearPendingNotes();

    for (auto pd : remaining) {
      size_t new_pos;
      if (pd.first >= frames) {
	new_pos = pd.first - frames;
      } else {
	new_pos = 0;
      }
      for (auto & [ id, tuning, note ] : pd.second) {       
	track.addPendingNote(new_pos, id, tuning, note);
      }
    }
    
    instrument.applyEffects(data);
    track.applyEffects(data);

    hrft.accumulate(buffer, frames, track.getVolume(), track.getDistance(), track.getAzimuth(), track.getElevation());
  }

  hrft.encode(out, frames, song.getMasterVolume());

  return master;
}
