# Equalizer: effect and UI

## State today
- `<equalizer>` exists as a draft (`effects/Equalizer.{h,cpp}`): 4 fixed bands,
  XML-only, unbuilt and untested.
- The UI has no way at all to add an effect to a track or to edit one's
  parameters: effects are authored in the song XML. The outline panel lists
  tracks and offers Delete/preview; Sends are the only per-track values edited
  in-app (ClipGrid Sends row). So the UI is new ground, not an extension.

## Effect (engine)
- More bands: 8, as a list rather than fixed attribute names. Band i has
  `type` (peak/lowshelf/highshelf/highpass/lowpass/notch), `freq`, `gain`
  (dB), `q`. Default bands: HP off, low shelf, 4 peaks, high shelf, LP off.
  Stored as `<band .../>` children (the node tree already supports child nodes;
  see `<generator>` under GenericInstrument) so the count is not baked into
  attribute names.
- A band at neutral (peak/shelf 0 dB, filters disabled) costs nothing.
- Optional later: combined frequency response (`Equalizer::responseDb(freq)`),
  used by the UI curve; computed from the same biquad coefficients.

## UI (the main part)
1. Add/remove: an "Add Effect" action on a Track row in the outline (button
   bar), choosing from a list of effect types (equalizer first, others
   follow); Delete removes it. Goes through `Song::addTrack()`/`removeTrack()`
   inside an `Edit`, so undo works.
2. Editor: opens from the effect's outline row (Enter). A modal/floating
   panel like InfoDialog/ChoiceDialog, toolkit-agnostic model in `ui/`
   (band list, selected band, value edits) and terminal rendering in
   `ui/tui/EqEditor`:
   - top: frequency-response curve, log frequency axis 20 Hz-20 kHz, +-24 dB,
     braille/pixel rendering like SpectrumMeter, bands as markers on the curve;
   - the live spectrum under the curve if cheap to share;
   - bottom: the selected band's type/freq/gain/Q as editable fields.
   - keys: Left/Right select band, Up/Down gain, Shift+Left/Right freq,
     +/- Q, `t` cycle type, `a`/`d` add/delete band, Escape closes. Mouse:
     click selects a band, drag moves freq (x) and gain (y), wheel adjusts Q.
   - every change is a `Controller` call -> `Song::editTrack()` (live, audible
     while playing, one undo step per drag/keypress run).
3. Commands: `add-effect`, `edit-effect` registered in `UI::initializeCommands()`
   (M-x and menu), editor itself terminal-specific.

## Order of work
1. Install libunistring-dev, get the draft building, tests passing.
2. Rework the effect to 8 typed bands + response function, tests incl. response
   vs. rendered gain.
3. Controller/UI add-remove effect; undo coverage.
4. The editor: model + terminal rendering + keys, then mouse.
5. Docs (`docs/effects.md`, `docs/terminal.md`), e2e script for the editor.

## Open
- Per-key insert (EQ on one drum of a PercussionTrack): decided to revisit at
  the end. Until then the workaround is a separate percussion track for the kick.
