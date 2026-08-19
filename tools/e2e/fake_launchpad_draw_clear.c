// Simulated Launchpad X exercising DRAW mode's "hue decided on release"
// design: a press never changes a pad's hue immediately, only its
// brightness (live, via press velocity + aftertouch); the hue decision
// happens on release, based on how long the pad was held and whether it
// was already lit:
//   - off -> on lands on the default hue either way (short or long).
//   - already lit + short release -> cycles to the next hue.
//   - already lit + long hold -> hue stays exactly as it was (brightness-
//     only adjustment).
// Also exercises the CC97-long-press canvas-clear gesture (the replacement
// for the CC99 corner "button", which isn't a real pressable control on
// real Launchpad X hardware). Prints every SysEx it receives so the Python
// driver can inspect the LED bytes after each step.
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
  else if (status == 0x80) snd_seq_ev_set_noteoff(&ev, 0, note, velocity);
  else if (status == 0xA0) {
    ev.type = SND_SEQ_EVENT_KEYPRESS;
    ev.data.note.channel = 0;
    ev.data.note.note = note;
    ev.data.note.velocity = velocity;
  }
  snd_seq_event_output_direct(seq, &ev);
}

static void drain_sysex(snd_seq_t * seq) {
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
}

int main() {
  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  snd_seq_set_client_name(seq, "Launchpad X");
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake Launchpad X (draw/clear) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  sleep(6); // let synth auto-connect, enter Programmer mode, and settle
  drain_sysex(seq);

  fprintf(stderr, "STEP enter-draw-mode: CC97 quick tap\n");
  send_cc(seq, port, 97, 127);
  usleep(100 * 1000);
  send_cc(seq, port, 97, 0);
  sleep(1);
  drain_sysex(seq);

  fprintf(stderr, "STEP off-to-on: press pad (0,0) v=50, quick release\n");
  send_note(seq, port, 0x90, 11, 50);
  usleep(200 * 1000);
  send_note(seq, port, 0x80, 11, 0);
  sleep(1);
  drain_sysex(seq);

  fprintf(stderr, "STEP short-click-cycles: press pad (0,0) v=50, quick release\n");
  send_note(seq, port, 0x90, 11, 50);
  usleep(200 * 1000);
  send_note(seq, port, 0x80, 11, 0);
  sleep(1);
  drain_sysex(seq);

  fprintf(stderr, "STEP long-hold-press: press pad (0,0) v=30 (hue must NOT change yet)\n");
  send_note(seq, port, 0x90, 11, 30);
  sleep(1);
  drain_sysex(seq);

  fprintf(stderr, "STEP long-hold-aftertouch: aftertouch v=90 mid-hold (brightness only)\n");
  send_note(seq, port, 0xA0, 11, 90);
  sleep(1);
  drain_sysex(seq);

  fprintf(stderr, "STEP long-hold-release: release after >600ms hold (hue must stay unchanged)\n");
  send_note(seq, port, 0x80, 11, 0);
  sleep(1);
  drain_sysex(seq);

  fprintf(stderr, "STEP clear-canvas: CC97 long hold (700ms) then release\n");
  send_cc(seq, port, 97, 127);
  usleep(700 * 1000);
  send_cc(seq, port, 97, 0);
  sleep(1);
  drain_sysex(seq);

  fprintf(stderr, "STEP off-long-hold-press: press pad (0,0) v=60 from OFF (must stay black while held)\n");
  send_note(seq, port, 0x90, 11, 60);
  sleep(1);
  drain_sysex(seq);

  fprintf(stderr, "STEP off-long-hold-release: release after >600ms hold (must land on default hue)\n");
  send_note(seq, port, 0x80, 11, 0);
  sleep(1);
  drain_sysex(seq);

  snd_seq_close(seq);
  return 0;
}
