# synth

A Microtonal Synth: tracker style music production system with microtonal notes.

# Properties:

1. Microtonal (31-TET)
2. Just tuning

# Principles:
1. User can start creating music instantly
  1. No low latency requirements
  2. Basic instruments are immediately available
    1. If there is no SoundFont, basic instruments (such as piano) are provided by the built in FM synthesis
2. No limitations
  1. Easy to use microtonality
3. Keyboard driven
  1. Everything can be done using keyboard without mouse
4. Exact
  1. Just tuning

# Roadmap / missing functionality:
1. Undo/redo
2. Effect-command interpretation during playback (slide, glide, vibrato, fade in/out, tremolo — currently editable and stored but not audible)
3. Kill-ring rotation (yank-pop / M-y)
4. Exchange-point-and-mark (C-x C-x)
