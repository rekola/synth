#ifndef _SCORECONTEXT_H_
#define _SCORECONTEXT_H_

#include "../doc/Document.h"

class SampleStore;

// What the score's views need from the song that owns them.
struct ScoreContext {
  doc::Document * document = nullptr;
  SampleStore * samples = nullptr;
};

#endif
