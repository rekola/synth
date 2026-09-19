// Simulated Launchpad X exercising CC91 ("move-row-up") as a held shift
// modifier: pressing a Session-view pad while it's held opens that pad's
// own clip for direct step-grid editing (LaunchpadManager::
// handleSessionPadEvent()'s own comment) instead of triggering/assigning
// it - mirroring the terminal's own "toggle-record-arm" drum-machine
// repurposing (Controller::toggleDrumClipFocus()), just reached from a
// completely different physical gesture - and that CC95 ("Session") alone
// then closes it again outright (Controller::closeDrumClipFocus()), not
// needing another shift+pad combo. Two phases, timed to let
// verify_launchpad_shift_stepgrid.py read the terminal's own ClipGrid
// text in between: phase 1 opens "Beat 1" (clip index 7, pad (0,0)),
// phase 2 (after the script's own mid-run screen read) closes it again
// with a lone CC95 press.
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
  fake_ready_init();
  snd_seq_t * seq;
  if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return 1;
  snd_seq_set_client_name(seq, "Launchpad X (e2e)"); // see LaunchpadIO::acceptsClient()
  int port = snd_seq_create_simple_port(seq, "Launchpad X MIDI 2",
    SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
    SND_SEQ_PORT_TYPE_APPLICATION);
  if (port < 0) return 1;
  fprintf(stderr, "fake Launchpad X (shift stepgrid) ready as client %d port %d\n", snd_seq_client_id(seq), port);

  fake_wait_ready(seq, "at startup");
  drain(seq, 500, "at startup"); // the first LED frames

  fprintf(stderr, "sending CC91 press (holding shift)\n");
  send_cc(seq, port, 91, 127);
  drain(seq, 300, "shift held");

  fprintf(stderr, "sending press+release on pad (0,0) [note 11] while shift held - opens clip index 7\n");
  send_note(seq, port, 0x90, 11, 100);
  send_note(seq, port, 0x80, 11, 0);
  drain(seq, 300, "clip opened");

  fprintf(stderr, "sending CC91 release - move-row-up must NOT fire, it was combined with a pad\n");
  send_cc(seq, port, 91, 0);
  drain(seq, 1000, "shift released, phase 1 settled");

  // Phase 1 done here - verify_launchpad_shift_stepgrid.py reads the
  // terminal's own ClipGrid text at this point, then lets this process
  // continue into phase 2 below. A wide window (not just enough for the
  // read itself) - phase 2 must not start until well after the script's
  // own read is guaranteed to have happened, or it risks reading phase
  // 2's already-closed state instead of phase 1's open one.
  fake_wait_go(seq, "waiting for phase 1 to be read");

  // Opening a clip switches every connected device to NOTES mode to show
  // its own step grid (Controller::toggleDrumClipFocus()'s own "opened"
  // callback, TerminalUI.cpp's forceNotesModeOnAllDevices()) - CC95
  // ("Session") closes it outright (Controller::closeDrumClipFocus(),
  // LaunchpadManager::handleRawButton()'s own CC95 case), the same effect
  // as pressing shift+pad again on the pad that opened it, just without
  // needing to know which pad that was or holding shift at all.
  fprintf(stderr, "sending CC95 press+release - closes the sequencer outright\n");
  send_cc(seq, port, 95, 127);
  send_cc(seq, port, 95, 0);
  drain(seq, 1000, "phase 2 settled");

  snd_seq_close(seq);
  return 0;
}
