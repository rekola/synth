#ifndef _PLAYBACKCONTROLEVENT_H_
#define _PLAYBACKCONTROLEVENT_H_

#include "Event.h"
#include "EventHandler.h"

#include <string>
#include <utility>

class PlaybackControlEvent : public Event {
 public:
  // SONG_CHANGED is gone - Player no longer keeps one global SongState
  // rebuilt from scratch on every buffer switch (that rebuild was also
  // what caused an XRUN on every switch - see Player.cpp's own history);
  // it now keeps one live SongState per buffer that's actually made sound
  // (see the per-buffer editing/playback-state plan's Part B), lazily
  // get-or-created directly off each event's own buffer_name, so no
  // separate "the active buffer changed" notification is needed at all.
  // BUFFER_KILLED/BUFFER_RENAMED replace the bookkeeping SONG_CHANGED used
  // to fold in for those two specific cases.
  // PLAY_SAMPLE_CLIP: live/Session-view triggering of a SampleTrack clip
  // (parameter1 = track_id, parameter2 = clip_index) - see Player.cpp's
  // own handler for the transport-driven counterpart it mirrors.
  //
  // PREVIEW_NOTE/PREVIEW_POOL_NOTE/PREVIEW_GROOVE/PREVIEW_STOP:
  // OutlineView's own instrument/groove audition path, before anything is
  // ever committed to a song (see Song::addInstrument()/OutlineView.cpp's
  // own NCKEY_ENTER handling for that separate, actually-persisted
  // action). Buffer-agnostic like TERMINATE/MIXER_CHANGED below (there is
  // no owning Track, let alone buffer - nothing being previewed is part
  // of any song yet, PREVIEW_POOL_NOTE included: it addresses a slot in
  // the current song's own instrument pool, not any track), so
  // buffer_name is repurposed: for PREVIEW_NOTE, a Library > Instruments
  // row's own literal/taxonomy name (whatever InstrumentProvider::
  // tryGetByLiteralName()/resolvePath() can resolve) with parameter1/
  // parameter2 the MIDI note value/velocity; for PREVIEW_POOL_NOTE,
  // buffer_name is unused and parameter1/parameter2/parameter3 carry the
  // Song > Instruments row's own pool index (InstrumentPool::getByIndex())/
  // MIDI note value/velocity instead - resolving the exact pool slot
  // rather than re-resolving a name means a slot's own generator
  // overrides or custom Oscillator parameters are heard exactly as
  // authored, not the provider's generic entry for that name; for
  // PREVIEW_GROOVE, a GroovePatternLibrary.h entry's own name
  // (findGroovePattern()). PREVIEW_NOTE/PREVIEW_POOL_NOTE/PREVIEW_GROOVE
  // each replace whatever that same kind of preview was already doing
  // outright (Player.h's own preview_note_voice_/preview_groove_pattern_ -
  // PREVIEW_NOTE and PREVIEW_POOL_NOTE share the one preview_note_voice_
  // slot, so starting either one retriggers the other); PREVIEW_STOP
  // releases all of them at once (a no-op for whichever weren't doing
  // anything).
  // GLIDE_TRACK_SEND_A/B/MAIN: the server-side-glide counterpart of
  // SET_TRACK_SEND_A/B/MAIN - a Launchpad fader press's own (target,
  // velocity-derived duration) triplet, not an instant value. parameter1 =
  // track_id, parameter2 = target in tenths of a dB (SET_TRACK_AZIMUTH's
  // own fixed-point convention, not SET_TRACK_SEND_A/B/MAIN's linear-gain
  // one - the engine's own ramp interpolates in dB, the same space the
  // Launchpad row layout this is driven from is itself linear in, so
  // that's what travels over the wire too, converted to linear gain only
  // once actually applied - see LeafTrackState.h's own comment on why),
  // parameter3 = duration in milliseconds. Handled by starting a
  // LeafTrackState::glideSendMain()/A()/B() ramp (Player.cpp) rather than
  // setting the value outright.
  // GLIDE_TRACK_AZIMUTH: Pan's own equivalent - parameter1 = track_id,
  // parameter2 = target degrees in tenths (SET_TRACK_AZIMUTH's own
  // fixed-point convention), parameter3 = duration in milliseconds.
  // Handled by starting a LeafTrackState::glideAzimuth() ramp, which picks
  // its own travel direction (see that method's own comment).
  enum Type { PLAY = 1, STOP, TERMINATE, MOVE_POSITION, CLEAR_VOICES, PLAY_NOTE, STOP_NOTE, STOP_ALL_NOTES, NOTE_PRESSURE, MIXER_CHANGED,
              SET_TRACK_MUTED, SET_TRACK_SOLO, SET_TRACK_SEND_A, SET_TRACK_SEND_B, SET_TRACK_SEND_MAIN, SET_TRACK_AZIMUTH,
              CHANNEL_PRESSURE, SET_RECORDING_MUTE, SET_POSITION, BUFFER_KILLED, BUFFER_RENAMED, SET_BUS_EFFECT,
              PLAY_SAMPLE_CLIP, PREVIEW_NOTE, PREVIEW_POOL_NOTE, PREVIEW_GROOVE, PREVIEW_STOP,
              GLIDE_TRACK_SEND_A, GLIDE_TRACK_SEND_B, GLIDE_TRACK_SEND_MAIN, GLIDE_TRACK_AZIMUTH };

  // buffer_name says which open buffer this event targets - required for
  // every type except the genuinely buffer-agnostic ones (TERMINATE,
  // MIXER_CHANGED: process-/device-wide, never song-specific; PREVIEW_NOTE/
  // PREVIEW_GROOVE: repurpose the field for a name instead, see Type's own
  // comment; PREVIEW_POOL_NOTE: repurposes parameter1 for a pool index
  // instead, buffer_name left empty), which simply leave it at its
  // default empty string (PREVIEW_NOTE/PREVIEW_GROOVE always set it, just
  // not to a real buffer name).
  PlaybackControlEvent(Type _type, std::string _buffer_name = "", int _parameter1 = 0, int _parameter2 = 0, int _parameter3 = 0, int _parameter4 = 0)
    : type(_type), buffer_name(std::move(_buffer_name)), parameter1(_parameter1), parameter2(_parameter2), parameter3(_parameter3), parameter4(_parameter4) { }

  // BUFFER_RENAMED only: buffer_name carries the *old* name (whatever
  // Player's own live_states_/playing_buffer_name_ still call it),
  // new_buffer_name the name it's being renamed to - Player rekeys
  // (rather than drops and re-creates) so a still-live SongState's voices/
  // release tail survive the rename, same as any other buffer switch.
  PlaybackControlEvent(Type _type, std::string _old_buffer_name, std::string _new_buffer_name)
    : type(_type), buffer_name(std::move(_old_buffer_name)), new_buffer_name(std::move(_new_buffer_name)) { }

  void dispatch(EventHandler & evh) override { evh.handlePlaybackControlEvent(*this); }

  Type getType() const { return type; }
  const std::string & getBufferName() const { return buffer_name; }
  const std::string & getNewBufferName() const { return new_buffer_name; }
  int getParameter1() const { return parameter1; }
  int getParameter2() const { return parameter2; }
  int getParameter3() const { return parameter3; }
  int getParameter4() const { return parameter4; }

 private:
  Type type;
  std::string buffer_name, new_buffer_name;
  int parameter1 = 0, parameter2 = 0, parameter3 = 0, parameter4 = 0;
};

#endif
