// Simulated Launchpad X exercising the per-track Record Arm mechanism's
// actual *recording* gesture for a SampleTrack specifically - not just the
// arm/disarm picker (already covered by fake_launchpad_record_arm_picker.c
// against a note track), but a real Session-view pad press on an armed
// SampleTrack actually arming real audio capture
// (Controller::armSessionTrackRecording()/armThresholdRecording()) instead
// of falling through to plain audition/assign (the `is_sample_track`
// carve-out LaunchpadManager::triggerSessionClip() used to have). Never
// touches NOTE mode or real audio - a SampleTrack's own capture is
// threshold-triggered, not something a scripted press can make arrive - so
// this only exercises the "armed and waiting" state transition, the same
// reasoning fake_launchpad_record_arm_picker.c already documents for why
// it never triggers real playback either.
//
// argv[1] == "cancel" sends a *second* Session-grid press on the same pad
// after the first (verify_launchpad_sampletrack_record_arm.py's own
// two-spawn design: one spawn with a single press proves arming shows the
// record indicator, a second spawn with this flag proves a second press on
// the same pad cancels it again) - omitted (the default) sends only the
// first.
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void send_cc(snd_seq_t * seq, int port, int cc, int value) {
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  snd_seq_ev_set_controller(&ev, 0, cc, value);
  snd_seq_event_output_direct(seq, &ev);
}

static void send_note(snd_seq_t * seq, int port, int status, int note, int velocity) {
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  if (status == 0x90) snd_seq_ev_set_noteon(&ev, 0, note, velocity);
  else snd_seq_ev_set_noteoff(&ev, 0, note, velocity);
  snd_seq_event_output_direct(seq, &ev);
}

static void drain(snd_seq_t * seq, int ms, const char * label) {
  for (int waited = 0; waited < ms; waited += 100) {
    usleep(100000);
    int pending;
    while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
      snd_seq_event_t * in_ev;
      snd_seq_event_input(seq, &in_ev);
      if (in_ev->type == SND_SEQ_EVENT_SYSEX) {
        fprintf(stderr, "received sysex %s (%d bytes):", label, in_ev->data.ext.len);
        unsigned char * data = (unsigned char *)in_ev->data.ext.ptr;
        for (unsigned int i = 0; i < in_ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
        fprintf(stderr, "\n");
      }
      snd_seq_free_event(in_ev);
    }
  }
}

int main(int argc, char ** argv) {
  int also_cancel = argc > 1 && strcmp(argv[1], "cancel") == 0;

  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  snd_seq_set_client_name(seq, "Launchpad X");
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake Launchpad X (sampletrack record arm%s) ready as client %d port %d\n",
    also_cancel ? ", cancel" : "", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle
  drain(seq, 500, "at startup");

  fprintf(stderr, "sending CC95 press+release (enters Session's own mixer submode)\n");
  send_cc(seq, port, 95, 127);
  send_cc(seq, port, 95, 0);
  drain(seq, 500, "mixer submode entered");

  fprintf(stderr, "sending CC19 press+release - opens the track-picker overlay (Record Arm)\n");
  send_cc(seq, port, 19, 127);
  send_cc(seq, port, 19, 0);
  drain(seq, 500, "picker opened");

  fprintf(stderr, "sending press on pad (0,0) [note 11] - picker row, column 0 - arms the track\n");
  send_note(seq, port, 0x90, 11, 100);
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 500, "track armed");

  fprintf(stderr, "sending CC19 press+release - closes the overlay, back to the plain Session grid\n");
  send_cc(seq, port, 19, 127);
  send_cc(seq, port, 19, 0);
  drain(seq, 500, "back on session grid, track armed but idle");

  fprintf(stderr, "sending press on pad (0,0) [note 11] - Session grid, clip index 7 (7-0) - arms real audio capture\n");
  send_note(seq, port, 0x90, 11, 100);
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 500, "sample capture armed, waiting on the loudness threshold");

  if (also_cancel) {
    fprintf(stderr, "sending press on pad (0,0) [note 11] again - cancels the still-idle arm\n");
    send_note(seq, port, 0x90, 11, 100);
    send_note(seq, port, 0x80, 11, 0);
    drain(seq, 500, "sample capture arm cancelled");
  }

  // Deliberately never reopens the picker to disarm the track - the final
  // state under test is whatever the Session-grid press(es) above left
  // behind, not a disarmed track (Controller::disarmTrack()'s own
  // trimSessionRecordingClip() call would erase session_recording_takes_
  // unconditionally either way, masking whether the cancel gesture itself
  // actually worked).
  drain(seq, 3000, "settled");

  snd_seq_close(seq);
  return 0;
}
