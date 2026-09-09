#include "GroovePatternLibrary.h"

#include <cassert>
#include <cstring>

using namespace std;

namespace {

// Standard General MIDI percussion key numbers (the same map
// GmInstrumentTable.h's own kGmBank128Table entries play through) - named
// here purely as authoring shorthand for the table below, not re-exported.
constexpr int KICK = 36;       // Bass Drum 1
constexpr int SNARE = 38;      // Acoustic Snare
constexpr int RIM = 37;        // Side Stick
constexpr int CLAP = 39;       // Hand Clap
constexpr int CHH = 42;        // Closed Hi-Hat
constexpr int PHH = 44;        // Pedal Hi-Hat
constexpr int OHH = 46;        // Open Hi-Hat
constexpr int RIDE = 51;       // Ride Cymbal 1
constexpr int CRASH = 49;      // Crash Cymbal 1
constexpr int COWBELL = 56;
constexpr int CLAVES = 75;
constexpr int SHAKER = 70;     // Maracas
constexpr int CONGA_HI = 62;
constexpr int CONGA_LO = 63;

// One instrument's own lane within a pattern, as a step string - one
// character per row, 'X' an accented hit, 'x' a plain one, anything else
// (conventionally '.') a rest. `steps` must be exactly `length` characters
// long (asserted, not just trusted) - the same 4-rows-per-beat grid this
// engine's own ChannelConfiguration::getRowDuration() already fixes
// (60/4/tempo), so a 16-character string is one 4/4 bar of 16th-note
// steps, a 12-character one a 3/4 bar (or a 6/8 one - same row count,
// different accent placement), and so on for whatever meter a given
// preset is actually in.
void addLane(vector<GroovePatternHit> & hits, int length, const char * steps, int note) {
  assert(static_cast<int>(strlen(steps)) == length);
  for (int i = 0; i < length && steps[i]; i++) {
    if (steps[i] == 'x') hits.push_back({ i, note, 95 });
    else if (steps[i] == 'X') hits.push_back({ i, note, 118 });
  }
}

} // namespace

const vector<GroovePatternTemplate> &
getGroovePatternLibrary() {
  static const vector<GroovePatternTemplate> library = [] {
    vector<GroovePatternTemplate> patterns;

    // --- Simple/compound meters other than plain 4/4, deliberately kept
    // together up top - these are what actually exercise a length other
    // than the usual 16-row bar (GroovePatternTemplate::length is
    // per-pattern, not a fixed constant, precisely for this).

    {
      vector<GroovePatternHit> hits;
      addLane(hits, 12, "X...........", KICK);
      addLane(hits, 12, "....x...x...", RIM);
      patterns.push_back({ "Waltz", "Grooves",
        "A 3/4 ballroom waltz - kick on beat 1, rim on beats 2 and 3.", 12, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 12, "X...........", KICK);
      addLane(hits, 12, "x.x.x.x.x.x.", RIDE);
      patterns.push_back({ "Jazz Waltz", "Grooves",
        "A swung, ride-cymbal-driven take on the 3/4 waltz.", 12, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 12, "X.....X.....", KICK);
      addLane(hits, 12, "......x.....", SNARE);
      addLane(hits, 12, "x.x.x.x.x.x.", CHH);
      patterns.push_back({ "Six-Eight", "Grooves",
        "A 6/8 compound-time groove - two groups of three, kick marking each one.", 12, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 20, "X...........X.......", KICK);
      addLane(hits, 20, "........x.......x...", SNARE);
      addLane(hits, 20, "x...x...x...x...x...", CHH);
      patterns.push_back({ "Five-Four", "Grooves",
        "A 5/4 groove felt as 3+2 - kick on beats 1 and 4, snare on 3 and 5.", 20, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 14, "X...X...X.....", KICK);
      addLane(hits, 14, "......x.....x.", RIM);
      addLane(hits, 14, "x.x.x.x.x.x.x.", CHH);
      patterns.push_back({ "Seven-Eight", "Grooves",
        "A 7/8 groove grouped 2+2+3, the most common way this meter is played.", 14, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 24, "X...........X...........", KICK);
      addLane(hits, 24, "......x...........x.....", SNARE);
      addLane(hits, 24, "x.x.x.x.x.x.x.x.x.x.x.x.", RIDE);
      patterns.push_back({ "Slow Rock", "Grooves",
        "A 12/8 ballad feel - kick on 1 and 3, snare on 2 and 4, ride on every eighth note.", 24, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 24, "X.....X.....X.....X.....", KICK);
      addLane(hits, 24, "......x...........x.....", SNARE);
      addLane(hits, 24, "x...x.x...x.x...x.x...x.", RIDE);
      patterns.push_back({ "Shuffle Blues", "Grooves",
        "A swung 12/8 blues shuffle, four-on-the-floor kick under a long-short ride.", 24, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 8, "X.......", KICK);
      addLane(hits, 8, "....X...", SNARE);
      addLane(hits, 8, "......x.", RIM);
      patterns.push_back({ "March", "Grooves",
        "A simple 2/4 marching-band beat - boom, chick.", 8, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 8, "X.......", KICK);
      addLane(hits, 8, "..x.X...", RIM);
      patterns.push_back({ "Polka", "Grooves",
        "A bouncy 2/4 oom-pah-pah.", 8, move(hits) });
    }

    // --- Plain 4/4, 16 rows - the bulk of a keyboard's own rhythm list.

    {
      vector<GroovePatternHit> hits;
      addLane(hits, 32, "X.......X.......X.......X...X...", KICK);
      addLane(hits, 32, "....X.......X.......X.......X...", SNARE);
      addLane(hits, 32, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", CHH);
      addLane(hits, 32, "X...............................", CRASH);
      patterns.push_back({ "8 Beat", "Grooves",
        "The basic rock/pop beat - kick on 1 and 3, snare on 2 and 4, straight eighth-note "
        "hi-hats - with a two-bar turnaround fill into the repeat.", 32, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X.......X.......", KICK);
      addLane(hits, 16, "....X.......X...", SNARE);
      addLane(hits, 16, "xxxxxxxxxxxxxxxx", CHH);
      patterns.push_back({ "16 Beat", "Grooves",
        "The busier companion to 8 Beat - the same kick/snare, but 16th notes on the hi-hat.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X...X...X...X...", KICK);
      addLane(hits, 16, "..x...x...x...x.", OHH);
      addLane(hits, 16, "....X.......X...", CLAP);
      patterns.push_back({ "Disco", "Grooves",
        "Four-on-the-floor kick, open hi-hats on the off-beats, clap backbeat.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "x.......x.......", KICK);
      addLane(hits, 16, "....x.......x...", PHH);
      addLane(hits, 16, "X..x.X..x.X..x..", RIDE);
      patterns.push_back({ "Swing", "Grooves",
        "The classic jazz ride-cymbal \"spang-a-lang\" pattern.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "x.......x.......", KICK);
      addLane(hits, 16, "....x.......x...", RIM);
      addLane(hits, 16, "x...x...x...x...", CHH);
      patterns.push_back({ "Foxtrot", "Grooves",
        "A gentle ballroom 4/4, understated next to a full rock beat.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X.......X.......", KICK);
      addLane(hits, 16, "....x.......x...", SNARE);
      patterns.push_back({ "Ballad", "Grooves",
        "A slow, sparse pop ballad - just kick and a soft snare, no hi-hat at all.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X.....X...X.....", KICK);
      addLane(hits, 16, "....X.......X.x.", SNARE);
      addLane(hits, 16, "xxxxxxxxxxxxxxxx", CHH);
      patterns.push_back({ "Funk", "Grooves",
        "A syncopated 16th-note funk groove, with a ghost note before the backbeat.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X.....X.X.......", KICK);
      addLane(hits, 16, "....X.......X...", SNARE);
      addLane(hits, 16, "x.x.x.x.x.x.x.x.", RIDE);
      patterns.push_back({ "Boogie", "Grooves",
        "A walking eighth-note boogie-woogie feel.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X.......X.......", KICK);
      addLane(hits, 16, "....X.......X...", SNARE);
      addLane(hits, 16, "x.x.x.x.x.x.x.x.", RIM);
      patterns.push_back({ "Country", "Grooves",
        "A steady \"train beat\" 4/4 groove.", 16, move(hits) });
    }
    {
      // The skank belongs on the off-*8th* notes (the "and" of each beat -
      // 4 hits/bar), not the off-*16th* ones (8 hits/bar, twice too busy
      // and the wrong subdivision entirely for a one-drop feel).
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "........X.......", KICK);
      addLane(hits, 16, "........X.......", SNARE);
      addLane(hits, 16, "..x...x...x...x.", CHH);
      patterns.push_back({ "Reggae", "Grooves",
        "One-drop reggae - kick and snare together on beat 3, hi-hat skanking on the off-beats.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "....X.......X...", KICK);
      addLane(hits, 16, "X..X..X...X.X...", RIM);
      addLane(hits, 16, "x.x.x.x.x.x.x.x.", SHAKER);
      patterns.push_back({ "Bossa Nova", "Grooves",
        "The standard Brazilian bossa nova cross-stick pattern, surdo kick, shaker underneath.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X...X...X...X...", KICK);
      addLane(hits, 16, "..x.x.......x.x.", RIM);
      addLane(hits, 16, "xxxxxxxxxxxxxxxx", SHAKER);
      patterns.push_back({ "Samba", "Grooves",
        "A fast Brazilian samba groove - syncopated tamborim over a steady surdo kick.", 16, move(hits) });
    }
    {
      // A real 2-bar (32-row) pattern, not just a repeated 1-bar loop:
      // the standard son clave (3-2) it's built on is itself a
      // 2-measure figure by definition - 3 strokes in one bar ("3-side":
      // beat 1, the "and" of beat 2, beat 4), 2 in the other ("2-side":
      // beats 2 and 3) - it can't be correctly written in a single bar
      // at all.
      vector<GroovePatternHit> hits;
      addLane(hits, 32, "X.......X.......X.......X.......", KICK);
      addLane(hits, 32, "X.....X.....X.......X...X.......", CLAVES);
      addLane(hits, 32, "..x.x.......x.x...x.x.......x.x.", CONGA_HI);
      patterns.push_back({ "Rumba", "Grooves",
        "An Afro-Cuban rumba built on the real two-bar son clave (3-2).", 32, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X.......X.......", KICK);
      addLane(hits, 16, "X..X..X.X..X....", COWBELL);
      addLane(hits, 16, "....x.......x...", CONGA_LO);
      patterns.push_back({ "Mambo", "Grooves",
        "A syncopated cowbell montuno figure over conga, mambo-style.", 16, move(hits) });
    }
    {
      // 2/4, 8 rows - merengue's own fast duple meter, not 4/4 like the
      // rest of this Latin cluster. Steady güira (here on shaker) under
      // the tambora's own signature accent on the "and" of beat 2.
      vector<GroovePatternHit> hits;
      addLane(hits, 8, "X...X...", KICK);
      addLane(hits, 8, "......X.", RIM);
      addLane(hits, 8, "xxxxxxxx", SHAKER);
      patterns.push_back({ "Merengue", "Grooves",
        "A fast 2/4 Dominican merengue - steady güira under a tambora accent.", 8, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X.......X.......", KICK);
      addLane(hits, 16, "....x.......x...", RIM);
      addLane(hits, 16, "X...X...X..XXX..", COWBELL);
      patterns.push_back({ "Cha Cha Cha", "Grooves",
        "Cha cha cha's own signature triplet tag right before beat 1.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X..X..X.X.......", KICK);
      addLane(hits, 16, "....x.......x...", RIM);
      patterns.push_back({ "Tango", "Grooves",
        "The habanera-derived tango rhythm.", 16, move(hits) });
    }
    {
      vector<GroovePatternHit> hits;
      addLane(hits, 16, "X..X..X.X.......", KICK);
      addLane(hits, 16, "....x.......x...", RIM);
      addLane(hits, 16, "x.x.x.x.x.x.x.x.", CHH);
      patterns.push_back({ "Beguine", "Grooves",
        "A Caribbean beguine groove, built on the same habanera figure as tango.", 16, move(hits) });
    }

    return patterns;
  }();
  return library;
}

const GroovePatternTemplate *
findGroovePattern(const string & name) {
  for (auto & pattern : getGroovePatternLibrary()) {
    if (pattern.name == name) return &pattern;
  }
  return nullptr;
}
