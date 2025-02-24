CC = gcc
CFLAGS = -pthread -Wall

all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o server

client: client.c
	$(CC) $(CFLAGS) client.c -o client

runserver: server
	./server

runclient: client
	./client

clean:
	rm -f server client
