#ifndef _CLIPHIGHLIGHT_H_
#define _CLIPHIGHLIGHT_H_

// A clip slot's transport/recording state (LaunchpadManager::
// clipHighlight()) - on a Launchpad, Live View's per-pad overlay
// (DeviceState::clip_highlight) on top of a clip's own identity-hue
// static color, matching the live-sequencer convention of a fixed green
// flash/pulse for "about to launch"/"currently playing" regardless of
// that hue, rather than a lighter/darker shade of it; the terminal's clip
// grid shows the same states.
// NONE/QUEUED/PLAYING/PAUSED are the plain (green) transport overlay
// every track uses normally - PAUSED a launched clip while the transport
// is paused, static rather than pulsing; ARMED_EMPTY/RECORD_QUEUED/RECORDING/
// RECORD_STOPPING are an armed track's own (red) recording overlay,
// reached instead of - never alongside - the plain three the moment
// Controller::isTrackArmed() is true for that track's column, since
// every pad press there is now record-oriented
// (SessionPlayer::triggerClip()'s own armed branch), not plain
// audition/launch: a pad is always exactly one of these eight states,
// never two at once, so one enum/one array covers it, not a pair of
// independently-tracked overlays that would otherwise have to agree on
// which one wins. ARMED_EMPTY is a genuinely new visual (an unarmed
// empty slot shows nothing at all); RECORD_QUEUED/RECORDING mirror
// QUEUED/PLAYING but for a take rather than a plain launch;
// RECORD_STOPPING is unique to the recording side - pressing the pad
// currently being recorded into again queues a stop for just that take,
// shown as a flash (the same lighting type RECORD_QUEUED uses) rather
// than a new color, to keep the palette small, but tracked as its own
// state since it overrides RECORDING rather than combining with it.
enum class ClipHighlight { NONE, QUEUED, PLAYING, PAUSED, ARMED_EMPTY, RECORD_QUEUED, RECORDING, RECORD_STOPPING };

#endif
