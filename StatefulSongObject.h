#ifndef _STATEFULSONGOBJECT_H_
#define _STATEFULSONGOBJECT_H_

#include "SongObject.h"
#include "TrackState.h"

class StatefulSongObject : public SongObject {
 public:
  StatefulSongObject() { }

  virtual std::unique_ptr<TrackState> createState(const ChannelConfiguration & config) const = 0;
};

#endif
