// Minimal simulated Launchpad X: an ALSA sequencer client named to match
// LaunchpadProtocol::modelFromDeviceName, used to verify synth's
// LaunchpadIO auto-detection/connection/dispatch end-to-end without real
// hardware. Prints any SysEx it receives (to confirm the Programmer-Mode
// and Device-Inquiry messages went out), then switches into NOTES mode,
// arms Record Arm, and sends a scripted press/aftertouch/release sequence
// on pad (0,0) = note 11 - a Launchpad press only ever writes into the
// pattern with Record Arm on, and arming it starts playback, so there is
// no "step entry while stopped" state to test any more (a plain press
// only ever auditions).
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

  // Wait for synth to start, scan, auto-connect, and (in the test
  // harness) switch to a fresh new buffer.
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

  // GridMode defaults to SESSION on every connected device - a plain
  // note-on there launches a Session View clip slot instead of entering a
  // note; CC96 selects NOTES mode instead. A press also only actually
  // writes into the pattern (rather than just auditioning) with Record
  // Arm on - "just play" vs. "store into the pattern" - reached here via
  // a quick CC98 tap (CC19 itself is a full member of the scene-launch/
  // mixer radio group now, not this command, see LaunchpadManager.h's
  // own GridMode comment).
  fprintf(stderr, "sending CC96 press (Note mode)\n");
  send_cc(seq, port, 96, 127);
  sleep(1);
  fprintf(stderr, "sending CC98 quick tap (Record Arm on)\n");
  send_cc(seq, port, 98, 127);
  usleep(100 * 1000);
  send_cc(seq, port, 98, 0);
  sleep(1);

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
