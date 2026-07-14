// Same simulated Launchpad X as fake_launchpad.c, but starts sending
// immediately (no initial sleep) - used to verify LIVE hotplug detection:
// this process is started AFTER musiceditor is already running, so
// musiceditor must notice the new ALSA client via its announce-port
// subscription rather than its startup-time scan.
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

void send_note(snd_seq_t * seq, int port, int status, int note, int velocity) {
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  if (status == 0x90) {
    snd_seq_ev_set_noteon(&ev, 0, note, velocity);
  } else if (status == 0x80) {
    snd_seq_ev_set_noteoff(&ev, 0, note, velocity);
  }
  snd_seq_event_output_direct(seq, &ev);
}

int main() {
  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    fprintf(stderr, "failed to open seq\n");
    return 1;
  }
  snd_seq_set_client_name(seq, "Launchpad X");
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) {
    fprintf(stderr, "failed to create port\n");
    return 1;
  }
  fprintf(stderr, "fake Launchpad X (hotplug) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  // Give musiceditor's announce-port subscription a moment to notice us
  // and complete the Programmer-Mode-enter handshake before we press.
  sleep(3);

  int pending;
  while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
    snd_seq_event_t * ev;
    snd_seq_event_input(seq, &ev);
    if (ev->type == SND_SEQ_EVENT_SYSEX) {
      fprintf(stderr, "received sysex (%d bytes):", ev->data.ext.len);
      unsigned char * data = (unsigned char *)ev->data.ext.ptr;
      for (unsigned int i = 0; i < ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
      fprintf(stderr, "\n");
    }
    snd_seq_free_event(ev);
  }

  fprintf(stderr, "sending press on pad (0,0) [note 11], velocity 100\n");
  send_note(seq, port, 0x90, 11, 100);
  sleep(2);
  fprintf(stderr, "sending release on pad (0,0)\n");
  send_note(seq, port, 0x80, 11, 0);
  sleep(1);

  snd_seq_close(seq);
  return 0;
}
