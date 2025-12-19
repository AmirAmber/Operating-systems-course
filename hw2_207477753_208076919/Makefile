# Makefile for HW2
CC = gcc
CFLAGS = -pthread -Wall -g

# The final executable name required by the PDF
TARGET = hw2

# Your source file
SRC = hw2amir.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.o count*.txt thread*.txt dispatcher.txt stats.txt