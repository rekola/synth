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
  enum Type { PLAY = 1, STOP, TERMINATE, MOVE_POSITION, CLEAR_VOICES, PLAY_NOTE, STOP_NOTE, NOTE_PRESSURE, MIXER_CHANGED,
              SET_TRACK_MUTED, SET_TRACK_SOLO, SET_TRACK_SEND_A, SET_TRACK_SEND_B, SET_TRACK_SEND_MAIN, SET_TRACK_AZIMUTH,
              CHANNEL_PRESSURE, SET_RECORDING_MUTE, SET_POSITION, BUFFER_KILLED, BUFFER_RENAMED };

  // buffer_name says which open buffer this event targets - required for
  // every type except the two genuinely buffer-agnostic ones (TERMINATE,
  // MIXER_CHANGED: process-/device-wide, never song-specific), which
  // simply leave it at its default empty string.
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
