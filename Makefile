OBJS = 4ksyna.o SDLAudio.o AlsaAudio.o
CC = g++

CPPFLAGS = -O1 -Wall -std=c++1z
LIBS = -lSDL -lm -lasound

all:	intro

intro:	$(OBJS)
	$(CC) $(LDFLAGS) $(CPPFLAGS) $(OBJS) $(LIBS) -o intro
clean:
	rm *.o
	rm intro

.PHONY:	clean
