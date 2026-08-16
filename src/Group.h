#ifndef _GROUP_H_
#define _GROUP_H_

#include "Track.h"

class Group : public Track {
 public:
  Group() : Track(TrackType::GROUP) { }

  const char * getElementName() const override { return "group"; }
};

#endif
