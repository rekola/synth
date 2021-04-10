OBJS = 4ksyna.o Synth.o AlsaAudio.o BasicInstrument.o FileInstrument.o TerminalUI.o FFT.o
CC = g++

CPPFLAGS = -O1 -Wall -std=c++1z
LIBS = -lm -lasound -lsndfile -lnotcurses++ -lnotcurses -lfmt

all:	intro

intro:	$(OBJS)
	$(CC) $(LDFLAGS) $(CPPFLAGS) $(OBJS) $(LIBS) -o intro
clean:
	rm *.o
	rm intro

.PHONY:	clean
