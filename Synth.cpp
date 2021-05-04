#include "Synth.h"

#include "SampleData.h"
#include "Tuner.h"
#include "TrackEventQueue.h"

#include <cmath>
#include <cassert>

using namespace std;

SampleData
Synth::play(Song & song, size_t frames) {
  SampleData master(2, frames);
  float * out = master.data();

  auto & mastertrack = song.getMasterTrack();
  auto & tracks = mastertrack.getChildren();

  Tuner tuner;
  TrackEventQueue track_events;

  auto & mixer = getMixer();
  mixer.reset();
  
#if 0
  size_t tick_frames = getTickInterval(song);
  if (tick_frames > frames) tick_frames = frames;
  auto sinterval = getSampleInterval(song);
#endif

  if (is_playing) {
    for (size_t i = 0; i < frames; i++) {
      if (samplepos == 0) {
	auto & pattern = song.getPattern(getPatternPosition());
	auto tuning = pattern.getTuning() != Tuning::INHERIT ? pattern.getTuning() : song.getTuning();
	int key = pattern.getKey() >= 0 ? pattern.getKey() : song.getKey();

	for (size_t col = 0; col < tracks.size(); col++) {
	  auto & notes = pattern.getNotes(col, getTrackPosition());
	  for (size_t j = 0; j < notes.size(); j++) {
	    if (notes[j].isDefined()) {
	      auto & note = notes[j];
	      float frequency, velocity;
	      if (note.isOff()) {
		frequency = velocity = 0.0f;
	      } else {
		frequency = tuner.getFrequency(tuning, key, note);
		velocity = note.getVelocityAsFloat();
	      }
	      float delay = song.getRandomizationFactor() * samplerate * rand() / RAND_MAX;
	      track_events.addPendingEvent(col, i, int(j), delay, frequency, velocity);
	    }
	  }
	}
      }

      moveForwardSample(song);
    }
  }

  for (size_t track_idx = 0; track_idx < tracks.size(); track_idx++) {
    auto & track = tracks[track_idx];
    auto & instrument = song.getInstrument(track.getInstrumentId());

    size_t num_channels = instrument.getNumChannels();
    assert(num_channels == 1);
    
    SampleData data(num_channels, frames);
    auto buffer = data.data();
    
    for (size_t i = 0; i < frames; ) {
      size_t render_size = frames - i;
      auto & pending = track_events.getPendingEvents(track_idx);
      if (!pending.empty()) {
	auto it = pending.begin();
	assert(i <= it->first);
	assert(i == 0 || i == it->first); 
	if (i == it->first) {
	  for (auto & ev : it->second) {
	    if (ev.isOff()) {
	      track.stopNote(ev.getId());
	    } else {
	      track.playNote(ev.getFrequency(), ev.getVelocity(), ev.getDelay(), instrument, ev.getId());
	    }
	  }
	  it = pending.erase(it);
	}
	if (it != pending.end() && it->first - i < render_size) render_size = it->first - i;
      }     
      
      for (auto & voice : track.getVoices()) {
	if (voice->isPlaying()) {
	  voice->render(buffer, render_size, i);
	}
      }

      i += render_size;
    }
    
    instrument.applyEffects(data);
    track.applyEffects(data);

    mixer.accumulate(buffer, frames, track.getVolume(), track.getDistance(), track.getAzimuth(), track.getElevation());
  }

  mixer.encode(out, frames, song.getMasterVolume());

  assert(track_events.empty());
  
  return master;
}
