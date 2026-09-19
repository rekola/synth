// Minimal simulated Launchpad X: an ALSA sequencer client named to match
// LaunchpadProtocol::modelFromDeviceName, used to verify synth's
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

static void send_cc(snd_seq_t * seq, int port, int cc, int value) {
  snd_seq_event_t ev;
  snd_seq_ev_clear(&ev);
  snd_seq_ev_set_source(&ev, port);
  snd_seq_ev_set_subs(&ev);
  snd_seq_ev_set_direct(&ev);
  snd_seq_ev_set_controller(&ev, 0, cc, value);
  snd_seq_event_output_direct(seq, &ev);
}

// Waits `ms` milliseconds, printing every SysEx synth sends meanwhile, so
// the test can check the LEDs after each step.
static void drain(snd_seq_t * seq, int ms, const char * label) {
  for (int waited = 0; waited < ms; waited += 100) {
    usleep(100000);
    while (snd_seq_event_input_pending(seq, 1) > 0) {
      snd_seq_event_t * ev;
      snd_seq_event_input(seq, &ev);
      if (ev->type == SND_SEQ_EVENT_SYSEX) {
        fprintf(stderr, "received sysex %s (%d bytes):", label, ev->data.ext.len);
        unsigned char * data = (unsigned char *)ev->data.ext.ptr;
        for (unsigned int i = 0; i < ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
        fprintf(stderr, "\n");
      }
      snd_seq_free_event(ev);
    }
  }
}

int main() {
  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
    fprintf(stderr, "failed to open seq\n");
    return 1;
  }
  snd_seq_set_client_name(seq, "Launchpad X (e2e)"); // see LaunchpadIO::acceptsClient()
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) {
    fprintf(stderr, "failed to create port\n");
    return 1;
  }

  fprintf(stderr, "fake Launchpad X ready as client %d port %d\n", snd_seq_client_id(seq), port);

  // Wait for synth to start, connect, and for the test to move the
  // cursor onto the percussion track.
  drain(seq, 8000, "while connecting");

  // Session is the default grid mode - Note mode (CC96) shows the note
  // grid, here the percussion layout. Record Arm (a quick CC98 tap) makes a
  // press write into the pattern, and starts playback.
  fprintf(stderr, "sending CC96 press (Note mode)\n");
  send_cc(seq, port, 96, 127);
  drain(seq, 1000, "after Note mode");
  fprintf(stderr, "sending CC98 quick tap (Record Arm on)\n");
  send_cc(seq, port, 98, 127);
  usleep(100 * 1000);
  send_cc(seq, port, 98, 0);
  drain(seq, 1000, "after Record Arm");

  fprintf(stderr, "sending press on pad (0,0) [note 11], velocity 100\n");
  send_note(seq, port, 0x90, 11, 100);
  drain(seq, 3000, "after press");

  fprintf(stderr, "sending release on pad (0,0)\n");
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 1000, "after release");

  snd_seq_close(seq);
  return 0;
}
