#ifndef _SCORESCHEMA_H_
#define _SCORESCHEMA_H_

#include "../doc/Schema.h"

// What the score's document nodes carry (see SongSchema.h for the song's
// own). Notes and commands are sparse: one node per event that is actually
// there, kept sorted by (row, column).
namespace scoreschema {

// type "pattern": a track's inline content, or a clip's own.
inline const doc::Prop<int> kPatternLength{"length", 0};
inline constexpr const char * kNotesSlot = "notes";
inline constexpr const char * kCommandsSlot = "commands";

// type "note". The value is stored whatever it is - an aftertouch entry has
// -1 - and a note-off has velocity 0, which is the default.
inline const doc::Prop<int> kNoteRow{"row", 0};
inline const doc::Prop<int> kNoteColumn{"col", 0};
inline const doc::Prop<int> kNoteValue{"value", -1};
inline const doc::Prop<int> kNoteVelocity{"velocity", 0};
inline const doc::Prop<int> kNoteDelay{"delay", 0};

// type "command".
inline const doc::Prop<int> kCommandRow{"row", 0};
inline const doc::Prop<int> kCommandColumn{"col", 0};
inline const doc::Prop<std::string> kCommandData{"data", "----"};

}  // namespace scoreschema

#endif
