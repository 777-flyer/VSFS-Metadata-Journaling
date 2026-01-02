CC = gcc
CFLAGS = -Wall -Wextra -g -std=c99

all: journal

journal: journal.c journal.h
	$(CC) $(CFLAGS) -o journal journal.c

clean:
	rm -f journal *.o

.PHONY: all clean

