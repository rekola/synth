// Simulated Launchpad X that presses the "next-track" (CC94) extra button,
// used to verify LaunchpadIO's SND_SEQ_EVENT_CONTROLLER decoding and the
// resulting command dispatch end-to-end. Switches to NOTES mode (CC96)
// first - GridMode defaults to SESSION, where "next-track"/"prev-track"
// are reserved (an unconditional no-op) and the arrow button LEDs go dark,
// since neither one does anything a performer looking at the Launchpad
// could ever see there (LaunchpadManager.cpp's own comment on both).
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
  fprintf(stderr, "fake Launchpad X (button) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle
  drain(seq, 500, "at startup");

  fprintf(stderr, "sending CC96 press+release (Note mode) - GridMode defaults to Session\n");
  send_cc(seq, port, 96, 127);
  send_cc(seq, port, 96, 0);
  drain(seq, 1000, "after CC96");

  fprintf(stderr, "sending CC94 press (next-track)\n");
  send_cc(seq, port, 94, 127);
  drain(seq, 2000, "after CC94 press");

  fprintf(stderr, "sending CC94 release\n");
  send_cc(seq, port, 94, 0);
  drain(seq, 2000, "after CC94 release");

  snd_seq_close(seq);
  return 0;
}
