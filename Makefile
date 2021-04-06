OBJS = 4ksyna.o
CC = gcc

CFLAGS = -s -Os -Wall 
LIBS = -lSDL  -lm

all:	intro

intro:	$(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o intro
	ls -l intro
clean:
	rm *.o
	rm final
	rm intro
	rm -rf dist srcdist

.PHONY:	clean
