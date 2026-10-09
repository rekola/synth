#ifndef _SCORECONTEXT_H_
#define _SCORECONTEXT_H_

#include "SampleStore.h"
#include "../doc/Document.h"

// What the score's views need from the song that owns them.
struct ScoreContext {
  doc::Document * document = &doc::Document::inert();
  SampleStore * samples = &SampleStore::inert();

  ScoreContext() = default;
  ScoreContext(doc::Document * d, SampleStore * s) : document(d), samples(s) { }
};

#endif
