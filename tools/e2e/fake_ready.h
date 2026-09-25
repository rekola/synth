// Shared by the fake_launchpad_*.c simulators: instead of sleeping a fixed
// time for synth to start up and connect, wait until it actually has -
// its Programmer-mode SysEx is the first thing it sends a Launchpad it
// connects to - and, when the driving script still has terminal setup to
// do before this simulator acts, until the script says so
// (harness.go(), a SIGUSR1; FAKE_WAIT_FOR_GO in the environment asks for
// it). Every SysEx received meanwhile is logged as
// "received sysex <label> (N bytes): ..." like the simulators' own
// drain()s, so no LED frame goes unlogged or piles up unread.
#ifndef FAKE_READY_H
#define FAKE_READY_H

#include <alsa/asoundlib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t fake_go_received = 0;

static inline void fake_on_go(int signum) {
  (void)signum;
  fake_go_received = 1;
}

// Call first thing in main(), before synth can possibly send the signal.
static inline void fake_ready_init(void) {
  if (getenv("FAKE_WAIT_FOR_GO")) signal(SIGUSR1, fake_on_go);
  else fake_go_received = 1;
}

static inline void fake_log_sysex(const snd_seq_event_t * ev, const char * label) {
  unsigned char * data = (unsigned char *)ev->data.ext.ptr;
  fprintf(stderr, "received sysex %s (%u bytes):", label, ev->data.ext.len);
  for (unsigned int i = 0; i < ev->data.ext.len; i++) fprintf(stderr, " %02x", data[i]);
  fprintf(stderr, "\n");
}

static inline void fake_wait_ready(snd_seq_t * seq, const char * label) {
  int connected = 0;
  // A generous ceiling - a script whose synth never comes up still ends.
  for (int waited = 0; waited < 30000 && !(connected && fake_go_received); waited += 50) {
    usleep(50000);
    while (snd_seq_event_input_pending(seq, 1) > 0) {
      snd_seq_event_t * ev;
      snd_seq_event_input(seq, &ev);
      if (ev->type == SND_SEQ_EVENT_SYSEX) {
        unsigned char * data = (unsigned char *)ev->data.ext.ptr;
        fake_log_sysex(ev, label);
        // f0 00 20 29 02 <model> 0e 01 f7 - Programmer mode on.
        if (ev->data.ext.len >= 8 && data[6] == 0x0e && data[7] == 0x01) connected = 1;
      }
      snd_seq_free_event(ev);
    }
  }
}

// Waits, logging like fake_wait_ready(), for synth's first LED frame
// (f0 00 20 29 02 <model> 03 ...) - for a simulator that checks the LEDs
// synth starts with, which can follow Programmer mode by a while under
// load.
static inline void fake_wait_leds(snd_seq_t * seq, const char * label) {
  int lit = 0;
  for (int waited = 0; waited < 10000 && !lit; waited += 50) {
    usleep(50000);
    while (snd_seq_event_input_pending(seq, 1) > 0) {
      snd_seq_event_t * ev;
      snd_seq_event_input(seq, &ev);
      if (ev->type == SND_SEQ_EVENT_SYSEX) {
        unsigned char * data = (unsigned char *)ev->data.ext.ptr;
        fake_log_sysex(ev, label);
        if (ev->data.ext.len >= 7 && data[6] == 0x03) lit = 1;
      }
      snd_seq_free_event(ev);
    }
  }
}

// Waits, logging like fake_wait_ready(), for the script's next go() - for
// a script that reads the screen between two of this simulator's steps.
static inline void fake_wait_go(snd_seq_t * seq, const char * label) {
  fprintf(stderr, "waiting for go (%s)\n", label);
  fake_go_received = 0;
  for (int waited = 0; waited < 30000 && !fake_go_received; waited += 50) {
    usleep(50000);
    while (snd_seq_event_input_pending(seq, 1) > 0) {
      snd_seq_event_t * ev;
      snd_seq_event_input(seq, &ev);
      if (ev->type == SND_SEQ_EVENT_SYSEX) fake_log_sysex(ev, label);
      snd_seq_free_event(ev);
    }
  }
}

#endif
