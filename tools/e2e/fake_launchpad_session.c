// Simulated Launchpad X exercising GridMode::SESSION - now the per-device
// default (LaunchpadManager::DeviceState::grid_mode), so unlike every other
// fake_launchpad_*.c here this one presses nothing to *enter* Session view.
// Arms Record Arm (CC19) first, then presses pad (0,0): x=0 (the fixture's
// only track), y=0 -> pool index 7 (see LaunchpadManager::
// handleSessionPadEvent's own y-flip comment) - to confirm the press
// assigns that pooled pattern into the current scene rather than falling
// through to ordinary NOTES-mode note entry.
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

int main() {
  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  snd_seq_set_client_name(seq, "Launchpad X");
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake Launchpad X (session) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle

  int pending;
  while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
    snd_seq_event_t * in_ev;
    snd_seq_event_input(seq, &in_ev);
    if (in_ev->type == SND_SEQ_EVENT_SYSEX) {
      fprintf(stderr, "received sysex (%d bytes):", in_ev->data.ext.len);
      unsigned char * data = (unsigned char *)in_ev->data.ext.ptr;
      for (unsigned int i = 0; i < in_ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
      fprintf(stderr, "\n");
    }
    snd_seq_free_event(in_ev);
  }

  fprintf(stderr, "sending CC19 press (Record Arm on)\n");
  send_cc(seq, port, 19, 127);
  sleep(1);
  fprintf(stderr, "sending CC19 release\n");
  send_cc(seq, port, 19, 0);
  sleep(1);

  fprintf(stderr, "sending press on pad (0,0) [note 11] in Session view, velocity 100\n");
  send_note(seq, port, 0x90, 11, 100);
  sleep(1);
  fprintf(stderr, "sending release on pad (0,0)\n");
  send_note(seq, port, 0x80, 11, 0);
  sleep(1);

  snd_seq_close(seq);
  return 0;
}
