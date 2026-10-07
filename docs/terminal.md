# Terminal workflow

The terminal UI follows Emacs: mark and point selection, kill and yank, and
M-x for any named command. An operation that is a copy or a move is done with
the kill ring rather than with a command of its own, so there is no
"duplicate" or "delete" command for clips.

## Kill, copy and yank

| Key | Command | What it does |
|---|---|---|
| M-w | `kill-ring-save` | Copy |
| C-w | `kill-region` | Cut |
| C-y | `yank` | Paste at the cursor |
| C-g | `keyboard-quit` | Cancel the selection |

Which thing they act on depends on the focused widget.

### Pattern editor

They act on the region: the note under the cursor, or the marked block of rows
and tracks (C-SPC or C-b sets the mark). To duplicate a track's notes, select
the track, copy it, move to another track and yank.

### Clip grid (Session view)

They act on the clip under the cursor.

- **Duplicate a clip:** copy it, move to another slot (the one below, say) and
  yank. The paste is an independent copy under a fresh id and replaces what is
  in the slot, together with that clip's arrangement placements.
- **Move a clip:** cut it, move, yank.
- **Between tracks:** yank only onto a track of the same kind, because note
  values mean different things under different tunings and audio only fits
  audio.
- **Delete a clip:** cut it. Del, Backspace and C-k do the same, so a deleted
  clip can always be yanked back. On an empty slot they remove the slot's stop
  button instead, which is not clip content and never reaches the clipboard.
- A clip that is playing is not pulled out from under the playhead: its track
  stops at the next bar and the clip is removed then.

## Other clip commands

Available from M-x and the Clip menu: `launch-clip`, `launch-scene`,
`stop-all-clips`, `back-to-arrangement`, `track-back-to-arrangement`,
`quantize-clip`, `toggle-stop-button`, `copy-to-clip` (new clip from the
selection) and `merge-clip-to-background`.

## Commands that only a controller uses

A command that only a pad controller dispatches, and that nothing else
registers, is named with a `pad-` prefix (`pad-next-track`, `pad-prev-track`).
The prefix is deliberately not `launchpad-`, because other devices can send
them too. A pad gesture whose terminal equivalent is copy, kill or yank gets
no command of its own: the device calls the underlying function directly. That
is why the Launchpad's delete never touches the clipboard.
See `launchpad.md` for the gestures.
