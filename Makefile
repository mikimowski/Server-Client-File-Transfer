TARGET: netstore-server netstore-client

CC = cc
CFLAGS = -Wall -O2
LFLAGS = -Wall

netstore-server.o: err.h

netstore-server: netstore-server.o err.o
	$(CC) $(LFLAGS) $^ -o $@

netstore-client: netstore-client.o err.o
	$(CC) $(LFLAGS) $^ -o $@

clean:
	rm -f netstore-server netstore-client *.o
