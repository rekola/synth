# Future ideas: GridMode::CUSTOM for pitched instruments

`GridMode::CUSTOM` (Custom/CC97, part of the Session/Note/Custom/Draw
exclusive four-way group) only drives the percussion lane picker today,
gated on the assigned track being a `PercussionTrack`. Pressing Custom on
anything else currently shows a blank grid. A few concrete directions for
what it could mean once a pitched `InstrumentTrack` (or `Arpeggiator`) is
assigned instead:

- **Scale/mode editor**: light up the current EDO's scale degrees against
  the isomorphic pad layout; tapping a pad toggles that degree in/out of
  the active scale - the same "tap to add/remove" gesture the lane picker
  already uses, applied to scale membership instead of GM notes.
- **Per-pad note remapping**: let an isomorphic layout's pad-to-note
  assignment be hand-tweaked per track, the same way lanes remap GM
  percussion notes today.
- **Instrument/preset browser**: page through `InstrumentPool` entries
  directly on the grid (a hardware-only alternative to
  `PatternEditor.cpp`'s Numpad Divide/Multiply cycle), useful with no
  terminal attached at all.
- **Arpeggiator step editor**: for an `Arpeggiator` track, a step-grid
  surface analogous to the drum machine's, editing arp steps instead of
  drum hits.
- **Effect parameter surface**: expose a per-track effect's key parameters
  (e.g. filter cutoff/resonance) as a bank of pads/faders, the same
  grid-repurposing shape Send A/B/Pan already use.

None of the above is built. This file exists purely to record the
brainstorm from the PercussionTrack/DrumMachineTrack merge - delete once
one of these actually gets designed and built (or superseded).
