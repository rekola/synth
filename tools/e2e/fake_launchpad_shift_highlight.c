// Simulated Launchpad X exercising the shift+pad combo's own LED
// feedback while held, *before* release actually commits anything
// (LaunchpadManager::refreshLeds()'s own GridMode::SESSION branch): CC91
// ("move-row-up") lights full bright white the instant it's held, and the
// pad it's combined with (DeviceState::row_up_shift_pending_pad) gets the
// identical bright-white treatment the moment it's pressed, still held -
// before its own release ever fires Controller::toggleDrumClipFocus() at
// all - so a performer sees both lit together, confirming what releasing
// will do. Never touches real audio/ALSA capture (a plain LED readback,
// same reasoning fake_launchpad_record_arm_picker.c's own docstring has
// for why this doesn't hit the sandboxed-environment real-audio-path
// flakiness documented in docs/known_bugs.md), so plain hex-byte LED
// checks are reliable here the way they aren't for a SampleTrack's own
// record-arm gesture.
#include <alsa/asoundlib.h>
#include <stdio.h>
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

int main() {
  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  snd_seq_set_client_name(seq, "Launchpad X");
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake Launchpad X (shift highlight) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle
  drain(seq, 500, "at startup");

  fprintf(stderr, "sending CC91 press (holding shift) - not yet combined with any pad\n");
  send_cc(seq, port, 91, 127);
  drain(seq, 500, "shift held, no pad yet");

  fprintf(stderr, "sending press (not release) on pad (0,0) [note 11] - still held\n");
  send_note(seq, port, 0x90, 11, 100);
  drain(seq, 500, "shift and pad both held");

  fprintf(stderr, "sending release on pad (0,0) - commits the combo (opens the clip)\n");
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 500, "pad released, clip opened");

  fprintf(stderr, "sending CC91 release\n");
  send_cc(seq, port, 91, 0);
  drain(seq, 500, "shift released");

  snd_seq_close(seq);
  return 0;
}
