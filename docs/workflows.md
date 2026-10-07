# Operations: terminal and Launchpad workflows

The terminal follows Emacs: a thing is copied with `M-w`, cut with `C-w` and
pasted with `C-y`, so an operation that has a copy-paste equivalent has no
command of its own there. A command that only exists to serve a pad gesture
is not registered in the terminal at all: the Launchpad calls the underlying
function directly. Names that the Launchpad dispatches but nothing else
registers carry the prefix `pad-` (not `launchpad-`: any pad controller could
use them).

| Operation | Terminal | Launchpad |
|---|---|---|
| Duplicate a clip | Clip grid: `M-w` on the clip, move to the slot below, `C-y` (overwrites what is there) | Shift + Volume held, press the clip pad (copies into the slot below) |
| Duplicate a track's content | Pattern editor: select the track, `M-w`, move to another track, `C-y` | none |
| Delete a clip | Clip grid: `C-w` (also Del, Backspace, `C-k`); the clip goes to the clipboard, so `C-y` brings it back. On an empty slot it removes the slot's stop button instead | Shift + Pan held, press the pad; never reaches the clipboard, one layer per press (clip, then stop button) |
| Move a clip | Clip grid: `C-w`, move, `C-y` | none |
| Copy a clip between tracks | Clip grid: `M-w`, move to a track of the same kind, `C-y` | none |
| Quantise a clip | M-x `quantize-clip` | Shift + Send A held, press the pad |
| Toggle a slot's stop button | M-x `toggle-stop-button` | Delete a pad on an empty slot |
| Select a clip | Move the clip grid cursor | Shift + pad |
| Next / previous track | Pattern editor's own track movement | CC94 / CC93 (`pad-next-track` / `pad-prev-track`) |
| Octave up / down | `octave-up` / `octave-down` | Same commands |
| Tempo, swing | M-x `tempo-increase`, `swing-increase`, ... | Tempo and Swing views |
| Record quantise, metronome | M-x `toggle-record-quantize`, `toggle-metronome` | Shift + Send A tap, shift + Solo |
| Launch a clip or scene | M-x `launch-clip`, `launch-scene`, Enter on a slot | Session pads, scene buttons |

Not every pad gesture has a terminal equivalent, and none has to: the
Launchpad-only ones (tempo and swing views, draw mode, lane picker, the
track-picker overlay) are not offered in the terminal.
