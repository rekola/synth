#ifndef _ABOUTTEXT_H_
#define _ABOUTTEXT_H_

// The About dialog's content, shared by every UI backend (see Markdown.h).
inline constexpr const char * kAboutTitle = "About";

inline constexpr const char * kAboutMarkdown = R"(# synth

A **microtonal tracker** and synthesizer: 12, 19, 31 and 53 notes to the octave, with songs stored as plain XML.

It mixes the keybindings of *Emacs*, the pattern editing of classic trackers and the clip launching of live sequencers.

Run synth --licenses to see the licenses of the third-party code it uses.

*Press Escape to close this dialog.*
)";

#endif
