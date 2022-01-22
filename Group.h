#ifndef _GROUP_H_
#define _GROUP_H_

#include "Track.h"

class Group : public Track {
 public:
  Group() : Track(GROUP) { }

  virtual std::string getElementName() const override { return "group"; }
};

#endif
