OBJS = main.o \
	UI.o \
	Synth.o \
	AlsaAudio.o \
	BasicInstrument.o \
	FileInstrument.o \
	TerminalUI.o \
	Chart.o \
	PatternEditor.o \
	Controller.o \
	FMInstrument.o \
	InstrumentList.o \
	SoundFont.o \
	Song.o \
	HRFT.o \
	Track.o \
	MasterTrack.o \
	tinyxml2.o

CC = g++

CPPFLAGS = -O1 -Wall -std=c++1z -Werror=return-type -Werror=conversion-null -Werror=parentheses -Werror=switch -Werror=address -Werror=trigraphs -Wpointer-arith -Wcast-qual -Wnon-virtual-dtor -ffast-math -fno-math-errno
# -fno-diagnostics-show-caret

CXXFLAGS+= -Werror=return-local-addr -Werror=multichar -Werror=enum-compare
LIBS = -lm -lasound -lsndfile -lnotcurses++ -lnotcurses -lnotcurses-core -lfmt -lfftw3 -lspatialaudio
all:	musiceditor

musiceditor:	$(OBJS)
	$(CC) $(LDFLAGS) $(CPPFLAGS) $(OBJS) $(LIBS) -o musiceditor
clean:
	rm *.o
	rm musiceditor

.PHONY:	clean
