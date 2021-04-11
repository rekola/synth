OBJS = 4ksyna.o Synth.o AlsaAudio.o BasicInstrument.o FileInstrument.o TerminalUI.o FFT.o Chart.o ScoreDisplay.o
CC = g++

CPPFLAGS = -O1 -Wall -std=c++1z -Werror=return-type -Werror=conversion-null -Werror=parentheses -Werror=switch -Werror=address -Werror=trigraphs -Wpointer-arith -Wcast-qual -Wnon-virtual-dtor
# -fno-diagnostics-show-caret                                                                                                    
CXXFLAGS+= -Werror=return-local-addr -Werror=multichar -Werror=enum-compare
LIBS = -lm -lasound -lsndfile -lnotcurses++ -lnotcurses -lfmt

all:	intro

intro:	$(OBJS)
	$(CC) $(LDFLAGS) $(CPPFLAGS) $(OBJS) $(LIBS) -o intro
clean:
	rm *.o
	rm intro

.PHONY:	clean
