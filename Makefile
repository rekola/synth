OBJS = main.o Synth.o AlsaAudio.o BasicInstrument.o FileInstrument.o TerminalUI.o Chart.o ScoreDisplay.o Controller.o FMInstrument.o Reverb.o
CC = g++

CPPFLAGS = -O1 -Wall -std=c++1z -Werror=return-type -Werror=conversion-null -Werror=parentheses -Werror=switch -Werror=address -Werror=trigraphs -Wpointer-arith -Wcast-qual -Wnon-virtual-dtor
# -fno-diagnostics-show-caret

CXXFLAGS+= -Werror=return-local-addr -Werror=multichar -Werror=enum-compare
LIBS = -lm -lasound -lsndfile -lnotcurses++ -lnotcurses -lfmt -lfftw3

all:	musiceditor

musiceditor:	$(OBJS)
	$(CC) $(LDFLAGS) $(CPPFLAGS) $(OBJS) $(LIBS) -o musiceditor
clean:
	rm *.o
	rm musiceditor

.PHONY:	clean
