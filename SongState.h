#ifndef _SONGSTATE_H_
#define _SONGSTATE_H_

#include "HRFT.h"

class SongState {
 public:
  SongState() { }

  Mixer & getMixer() { return hrft; }

 private:
  HRFT hrft;
};

#endif
