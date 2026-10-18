// Simulated Launchpad X recording fader automation into a Live View
// take: arms the fixture's only track through the track-picker overlay's
// Record Arm purpose (mixer submode, CC19, pad (0,0), CC19 again), leaves
// mixer submode (CC95), presses an empty clip slot of that track (pad
// (0,7), clip 0) to start a take - which starts the transport, the take
// starting on its first bar - then, once it records, enters Send A fader
// mode (CC95, CC69) and presses a fader pad on the track's column (pad
// (0,2)), which should land as a YAxy command in the take's own clip.
#include <alsa/asoundlib.h>
#include "fake_ready.h"
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
      if (in_ev->type == SND_SEQ_EVENT_SYSEX) fake_log_sysex(in_ev, label);
      snd_seq_free_event(in_ev);
    }
  }
}

static void tap_cc(snd_seq_t * seq, int port, int cc, const char * what, int ms) {
  fprintf(stderr, "sending CC%d press+release (%s)\n", cc, what);
  send_cc(seq, port, cc, 127);
  send_cc(seq, port, cc, 0);
  drain(seq, ms, what);
}

static void tap_pad(snd_seq_t * seq, int port, int note, const char * what, int ms) {
  fprintf(stderr, "sending press+release on note %d (%s)\n", note, what);
  send_note(seq, port, 0x90, note, 100);
  send_note(seq, port, 0x80, note, 0);
  drain(seq, ms, what);
}

int main() {
  fake_ready_init();
  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  snd_seq_set_client_name(seq, "Launchpad X (e2e)"); // see LaunchpadIO::acceptsClient()
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake Launchpad X (live automation) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  fake_wait_ready(seq, "at startup");
  drain(seq, 500, "at startup");

  tap_cc(seq, port, 95, "enters mixer submode", 500);
  tap_cc(seq, port, 19, "opens the Record Arm picker", 500);
  tap_pad(seq, port, 11, "arms track 0", 500);
  tap_cc(seq, port, 19, "closes the picker", 500);
  tap_cc(seq, port, 95, "leaves mixer submode", 500);
  tap_pad(seq, port, 81, "starts a take in clip 0", 3000); // past the first bar
  tap_cc(seq, port, 95, "enters mixer submode", 500);
  tap_cc(seq, port, 69, "enters Send A fader mode", 500);
  tap_pad(seq, port, 31, "moves track 0's Send A fader", 1000);

  snd_seq_close(seq);
  return 0;
}
