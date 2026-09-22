CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE
TARGETS = server client

all: $(TARGETS)

server: server.c protocol.h
	$(CC) $(CFLAGS) -o server server.c

client: client.c protocol.h
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f $(TARGETS)

.PHONY: all clean
