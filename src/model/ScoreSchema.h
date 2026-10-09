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

// type "pattern", as a child of the arrangement: whose background it is.
inline const doc::Prop<int> kPatternTrack{"track", 0};

// type "command".
inline const doc::Prop<int> kCommandRow{"row", 0};
inline const doc::Prop<int> kCommandColumn{"col", 0};
inline const doc::Prop<std::string> kCommandData{"data", "----"};

// type "clip", by position in the root's "clips:<track id>" slot - a scene
// row. A clip owns one "pattern" node (slot "pattern") and any number of
// "sample" layers (slot "samples"), in take order.
inline const doc::Prop<std::string> kClipId{"id", ""};
inline const doc::Prop<std::string> kClipName{"name", ""};
inline const doc::Prop<int> kClipTrack{"track", 0};
inline const doc::Prop<bool> kClipLoop{"loop", true};
inline const doc::Prop<int> kClipLength{"length", 0};
inline const doc::Prop<bool> kClipStopButton{"stop", true};
inline constexpr const char * kClipPatternSlot = "pattern";
inline constexpr const char * kClipSamplesSlot = "samples";

// type "sample": one layer of audio, or a track's background bed. The audio
// itself is an asset (SampleStore); this says which, and how to play it.
inline const doc::Prop<int64_t> kSampleAsset{"asset", 0};
inline const doc::Prop<float> kSampleIn{"in", 0.0f};
inline const doc::Prop<float> kSampleOut{"out", 0.0f};
inline const doc::Prop<int> kSampleOriginalTempo{"originalTempo", 0};
inline const doc::Prop<int> kSampleNativeRate{"nativeRate", 0};
inline const doc::Prop<int> kSampleTrack{"track", 0};  // backgrounds only

// type "arrangement": the one timeline, the song root's only child there.
inline constexpr const char * kArrangementSlot = "arrangement";
inline constexpr const char * kArrangementPatternsSlot = "patterns";
inline constexpr const char * kArrangementInstancesSlot = "instances";
inline constexpr const char * kArrangementBackgroundsSlot = "backgrounds";

// type "instance", sorted by (track, row): the clip that starts at a row of a
// track, or an explicit stop. The clip is named by its id ("OFF" = a stop).
inline const doc::Prop<int> kInstanceTrack{"track", 0};
inline const doc::Prop<int> kInstanceRow{"row", 0};
inline const doc::Prop<std::string> kInstanceClip{"clip", ""};

}  // namespace scoreschema

#endif
