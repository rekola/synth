#ifndef _GROOVEPATTERNLIBRARY_H_
#define _GROOVEPATTERNLIBRARY_H_

#include <string>
#include <vector>

// One hit: `row` is a step index into its own GroovePatternTemplate::length
// (this engine's own row grid already puts 4 rows per beat -
// ChannelConfiguration::getRowDuration() - so a 16-row template is exactly
// one bar of 16th-note steps, the same grid every drum-machine/keyboard
// pattern is conventionally written in). `note` is a General MIDI
// percussion key number (the standard GM drum map - 36 = Bass Drum 1,
// 38 = Acoustic Snare, 42 = Closed Hi-Hat, ...), resolved against
// whatever kit the destination track/preview actually plays through,
// exactly like any other percussion note in this engine.
struct GroovePatternHit {
  int row;
  int note;
  int velocity;
};

// A single named, built-in rhythm template - OutlineView's own Library >
// <group> rows (e.g. "Grooves"). Deliberately not a Clip (Clip.h) -
// a Clip is always keyed to one specific, already-existing leaf track
// (Clip::getLeafTrackId()), but a library template exists independently
// of any song at all; "Add to Song" (OutlineView.cpp) is what turns one
// into a real Clip on a real (created-if-needed) PercussionTrack.
//
// Percussion-only today (`hits` all target one GM kit) - not called a
// "drum" pattern, though, since a real arranger-keyboard style is never
// just its drum part: the bass line (and often a chord comp) locks to
// the exact same groove and is meant to arrive with it, not as a
// separately hunted-down item. Nothing here commits to *how* that
// eventually looks (a second, differently-targeted hit list; a
// std::vector of named parts; ...) - that's follow-up work once real
// bass-line content actually exists to model, not something to guess at
// preemptively - but the type is named for the whole style/groove, not
// its drum part alone, so adding it later doesn't force another rename.
struct GroovePatternTemplate {
  std::string name;
  std::string group;
  // A short, user-facing sentence describing the groove - what
  // OutlineView.cpp shows for it (its own action panel/detail area), not
  // an implementation comment.
  std::string description;
  int length; // rows/steps
  std::vector<GroovePatternHit> hits;
};

// Every built-in template, grouped and named after real, widely-taught
// genre/style rhythms (not invented ones) - the kind of built-in rhythm
// list home/arranger keyboards have shipped with since the 1980s (Waltz,
// Swing, Bossa Nova, ...), not a transcription of any one specific
// recording. Built once, on first use (function-local static) - read-only
// afterward, safe to call from any thread (OutlineView.cpp populates the
// Library list from it; Player.cpp resolves a preview/add-to-song request
// against the same table by name, so the two can never disagree about
// what a given name means).
const std::vector<GroovePatternTemplate> & getGroovePatternLibrary();

// Looks a template up by its own name (getGroovePatternLibrary() is small
// and rebuilt-once, so a linear scan is cheap enough not to need a name
// index) - nullptr if `name` doesn't match any built-in template. Returns
// a pointer into the function-local static table above, valid for the
// life of the process.
const GroovePatternTemplate * findGroovePattern(const std::string & name);

#endif
