# Makefile - Build TCP chat server and client

CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -g
LDFLAGS = -pthread

SERVER  = server
CLIENT  = client

all: $(SERVER) $(CLIENT)

$(SERVER): server.c
	$(CC) $(CFLAGS) server.c -o $(SERVER) $(LDFLAGS)

$(CLIENT): client.c
	$(CC) $(CFLAGS) client.c -o $(CLIENT) $(LDFLAGS)

clean:
	rm -f $(SERVER) $(CLIENT) *.o

