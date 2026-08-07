# Pattern effect commands

A pattern row's effect column holds a 4-character command: a 2-character
mnemonic followed by a 2-digit hex argument (or two separate hex nibbles,
where noted) - e.g. `ZB00`. Character validation in the editor is
permissive (any letter, not just the mnemonics below), but only the
commands listed as **Implemented** actually do anything during playback;
everything else is accepted and stored but currently a no-op (see
`SongState.h`'s own command-handling loop).

## Implemented

| Command | Description |
|---|---|
| `ZBxx` | Pattern break - jump straight to row `xx` of the next pattern instead of playing out the rest of this one. |

## Planned

| Command | Description |
|---|---|
| `0Uxx` | Slide pitch up |
| `0Dxx` | Slide pitch down |
| `0Gxx` | Glide to note |
| `1Vxx` | Vibrato |
| `1Ixx` | Fade in |
| `1Oxx` | Fade out |
| `1Txy` | Tremolo (depth `x`, speed `y`) |
