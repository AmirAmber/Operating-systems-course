# Compiler
CC = gcc

# Compiler flags
# -g: adds debugging information
# -Wall: turns on most compiler warnings
CFLAGS = -g -Wall

# The build target executable:
TARGET = hw1shell

all: $(TARGET)

$(TARGET): hw1shell.c
	$(CC) $(CFLAGS) -o $(TARGET) hw1shell.c

clean:
	rm -f $(TARGET)
