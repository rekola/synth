#ifndef _STATEFULSONGOBJECT_H_
#define _STATEFULSONGOBJECT_H_

#include "SongObject.h"
#include "../state/TrackState.h"
#include "SongStructure.h"

class StatefulSongObject : public SongObject {
 public:
  StatefulSongObject() { }

  // structure: this track's stable ordinal (see SongStructure.h) -
  // only a handful of overrides (per-track Effect subclasses wanting a
  // NoteCoordinate at construction time) actually read it, but every
  // caller must supply a real one rather than relying on a silent empty
  // default.
  virtual std::unique_ptr<TrackState> createState(const ChannelConfiguration & config, const SongStructure & structure) const = 0;
};

#endif
