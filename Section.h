#ifndef _SECTION_H_
#define _SECTION_H_

#include "SongObject.h"

#include <vector>

class Section : public SongObject{
 public:
  Section() { }

  void addPattern(int id) { pattern_ids_.push_back(id); }
  
 private:
  std::vector<int> pattern_ids_;
};

#endif

