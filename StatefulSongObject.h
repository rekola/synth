#ifndef _STATEFULSONGOBJECT_H_
#define _STATEFULSONGOBJECT_H_

#include "SongObject.h"
#include "TrackState.h"

class StatefulSongObject : public SongObject {
 public:
  StatefulSongObject() { }
  StatefulSongObject(int id) : SongObject(id) { }
  StatefulSongObject(std::string name) : SongObject(std::move(name)) { }

  virtual std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const = 0;
};

#endif
