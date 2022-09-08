# MVerb

MVerb is a high quality reverb designed by Martin Eastwood. Four
reverbs are used in series in a figure 8 feedback loop with modulated
delays. This design is rumored to have been used on some of the high
end Lexicon hardware units and it definitely has that “American” sound
that is not only capable of lush and dense but also special effects
reverbs.

Here is a description of the MVerb’s parameters:

## Damping

Sets the amount of high frequencies that will be cut. You can set it
depending on the kind of room you are trying to simulate:

- A soft room (a room full of people/obstructions/soft materials) will
  lose a lot of its high frequencies.

- A hard room (a room with no people/obstructions/hard materials) will
  keep all of its high frequencies.

Following these principals, if we set the damping high then it will be
a softer room. Damping affects the overall tone and setting it
inversely to the source often works well: a lot of damping on a bright
sound will be warmer, but low damping on a duller sound can add some
air.

## Bandwidth

Sets the frequency range at which the damping will occur. You can
think of it as an overall material control: the less bandwidth you use
the softer the materials in the room will be, the warmer or less harsh
the reverb will be as a whole.

## Pre Delay

This is the amount of time it takes for the sound to create its first
reflection and is the first timed or rhythmic element in your
reverb. For a free running reverb, it is common to set this between
100th to a 10th of the decay size, however you can also use large pre
delay sizes if you are trying to replicate very large rooms or
halls. For rhythmic reverb we will use a BPM of 120 as an example:

1/4 note is 500ms

1/8 note is 250ms

16th note is 125ms

32nd note is 63ms

64th note is 32ms

To create a rhythmic pre delay at 120 BPM you can use one of these
values, in most circumstances a 32nd note or 64th note will give the
reverb the feeling of still being connected to the source sound. You
can notice that the above values have been rounded up: this can create
a feeling of playing slightly ahead or after the beat (“in the pocket”
effect). Adding or removing 1 or 2 milliseconds can push this effect
deeper.

## Density

This is the space between the first reflection and all the following
reflection: high density gives closer reflections and thicker
reverberation. For sustained sounds like vocals, a lower density will
give a much smoother reverb without becoming muddy. For a rhythmic or
percussive source, a higher density will give a more defined reverb
that removes bounciness.

## Decay

This determines how long it takes for the reflections in the room to
fade away. You can think of this as a strength rather than a length
control for the reverb tail: a longer decay will give a harder or
denser reverb tail, and a shorter decay will soften or thin out the
tail. It is common for decay time to be linked to room size, so a
small room will be given a small decay and vice versa. However, this
rule should not always be followed, especially if you are looking for
a more peculiar reverb.

On a large room a large decay can give a very dense reverb.

On a small room a large decay may result in a very metallic sounding
reverb.

You can use the rhythmic calculations as described in the pre delay
section to create a suitable stepped size for “in the pocket” set
ups. A wider range of settings and results can be obtained by setting
it in combination with the room size.

## Room Size

Sets the size of the room. A lower value simulates a small room while
a higher value simulates a bigger room. Combined with a large pre
delay, it is possible to create rooms as large as a hall or
church. For rhythmic reverbs you can look at this as a tightness
control: once you have set your pre delay to follow the BPM, you can
use this value to control the tightness or space of the reverb between
the notes.

## Gain

Sets the overall level of the output of the reverb.

## Early Mix

Sets the level of the very first reflections that occur when the sound
hits the surfaces in the room. These reflections can sound a little
like echo on a percussive or rhythmic source but can add depth on a
sustained source like vocals. Adjusting the early mix and decay at the
same time should give you some control over placement of sound from
the front to back of the room.

## Mix

This is the dry/wet control.

At 0%, no reverb effect is applied to the sound source.

At 100%, only the reverberated sound can be heard.

You can use this parameter to mix in how much reverb you want to hear
with the original source sound. When using reverb as a Send effect,
this value would typically be set to 100%: the dry level is set by the
source channel in the mixer, and the reverb level is set by the send
control.
