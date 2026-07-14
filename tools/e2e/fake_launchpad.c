// Minimal simulated Launchpad X: an ALSA sequencer client named to match
// LaunchpadProtocol::modelFromDeviceName, used to verify musiceditor's
// LaunchpadIO auto-detection/connection/dispatch end-to-end without real
// hardware. Prints any SysEx it receives (to confirm the Programmer-Mode
// and Device-Inquiry messages went out), then sends a scripted
// press/aftertouch/release sequence on pad (0,0) = note 11.
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
  } else if (status == 0xA0) {
    ev.type = SND_SEQ_EVENT_KEYPRESS;
    ev.data.note.channel = 0;
    ev.data.note.note = note;
    ev.data.note.velocity = velocity;
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

  fprintf(stderr, "fake Launchpad X ready as client %d port %d\n", snd_seq_client_id(seq), port);

  // Wait for musiceditor to start, scan, auto-connect, and (in the test
  // harness) switch to a fresh new song via Ctrl-N.
  sleep(6);

  // Drain and print any incoming SysEx (Programmer Mode enter / Device Inquiry).
  int pending;
  while ((pending = snd_seq_event_input_pending(seq, 1)) > 0) {
    snd_seq_event_t * ev;
    snd_seq_event_input(seq, &ev);
    if (ev->type == SND_SEQ_EVENT_SYSEX) {
      fprintf(stderr, "received sysex (%d bytes):", ev->data.ext.len);
      unsigned char * data = (unsigned char *)ev->data.ext.ptr;
      for (unsigned int i = 0; i < ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
      fprintf(stderr, "\n");
    } else {
      fprintf(stderr, "received event type %d\n", ev->type);
    }
    snd_seq_free_event(ev);
  }

  fprintf(stderr, "sending press on pad (0,0) [note 11], velocity 100\n");
  send_note(seq, port, 0x90, 11, 100);
  sleep(5);

  fprintf(stderr, "sending aftertouch on pad (0,0), pressure 90\n");
  send_note(seq, port, 0xA0, 11, 90);
  sleep(5);

  fprintf(stderr, "sending release on pad (0,0)\n");
  send_note(seq, port, 0x80, 11, 0);
  sleep(1);

  snd_seq_close(seq);
  return 0;
}
