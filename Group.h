#ifndef _GROUPTRACK_H_
#define _GROUPTRACK_H_

#include "Track.h"

class GroupTrack : public Track {
 public:
  GroupTrack() : Track(GROUP) { }

  virtual std::string getElementName() const override { return "group"; }
};

#endif
