# Terminal workflow

| Key | Command | Action |
|---|---|---|
| M-w | `kill-ring-save` | Copy |
| C-w | `kill-region` | Cut |
| C-y | `yank` | Paste at the cursor |
| C-g | `keyboard-quit` | Cancel the selection |

## Pattern editor

Acts on the region (C-SPC or C-b sets the mark). To duplicate a track's notes,
select the track, copy it, move to another track and yank.

## Clip grid

Acts on the clip under the cursor.

- Duplicate: copy, move to another slot, yank. The paste replaces what is in
  the slot, with its arrangement placements.
- Move: cut, move, yank.
- Yank only lands on a track of the same kind.
- Delete: cut. Del, Backspace and C-k do the same. On an empty slot they
  remove its stop button.
- A playing clip is removed at the next bar.

Other clip commands: `launch-clip`, `launch-scene`, `stop-all-clips`,
`back-to-arrangement`, `track-back-to-arrangement`, `quantize-clip`,
`toggle-stop-button`, `copy-to-clip`, `merge-clip-to-background`.

## Pad-only commands

Commands only a pad controller dispatches are prefixed `pad-`
(`pad-next-track`, `pad-prev-track`).
