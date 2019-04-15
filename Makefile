TARGET: netstore-server netstore-client

CC = cc
CFLAGS = -Wall -O2
LFLAGS = -Wall

server.o: err.h
client.o: err.h

netstore-server: server.o err.o
	$(CC) $(LFLAGS) $^ -o $@

netstore-client: client.o err.o
	$(CC) $(LFLAGS) $^ -o $@

clean:
	rm -f server client *.o
