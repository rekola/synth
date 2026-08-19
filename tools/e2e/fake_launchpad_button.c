// Simulated Launchpad X that presses the "next-track" (CC94) extra button,
// used to verify LaunchpadIO's SND_SEQ_EVENT_CONTROLLER decoding and the
// resulting command dispatch end-to-end.
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <unistd.h>

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

  snd_seq_event_t ev;

  fprintf(stderr, "sending CC94 press (next-track)\n");
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  snd_seq_ev_set_controller(&ev, 0, 94, 127);
  snd_seq_event_output_direct(seq, &ev);
  sleep(2);

  fprintf(stderr, "sending CC94 release\n");
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  snd_seq_ev_set_controller(&ev, 0, 94, 0);
  snd_seq_event_output_direct(seq, &ev);
  sleep(2);

  snd_seq_close(seq);
  return 0;
}
