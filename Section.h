#ifndef _SECTION_H_
#define _SECTION_H_

#include "Sequence.h"

#include <vector>

class Section {
 public:
  explicit Section() { }

  const std::vector<Sequence> & getSequences() const { return sequences; }
  const Sequence & getSequence(size_t i) const { return i < sequences.size() ? sequences[i] : empty_sequence; }
  Sequence & getSequence(size_t i) { return i < sequences.size() ? sequences[i] : empty_sequence; }
  void addSequence(const Sequence & s) { sequences.push_back(s); }
  size_t size() const { return sequences.size(); }
    
private:
  std::vector<Sequence> sequences;
  Sequence empty_sequence;
};

#endif
