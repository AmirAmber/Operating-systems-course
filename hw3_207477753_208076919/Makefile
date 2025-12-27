# Compiler and flags
CC = gcc
CFLAGS = -Wall -g

# Targets
all: hw3server hw3client

# Server build
hw3server: hw3server.c
	$(CC) $(CFLAGS) -o hw3server hw3server.c

# Client build
hw3client: hw3client.c
	$(CC) $(CFLAGS) -o hw3client hw3client.c

# Clean up
clean:
	rm -f hw3server hw3client *.o

.PHONY: all clean