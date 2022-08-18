#ifndef _SECTION_H_
#define _SECTION_H_

#include "SongObject.h"

#include <vector>

class Section : public SongObject{
 public:
  Section() { }
  
 private:
  std::vector<int> patterns;
};

#endif

