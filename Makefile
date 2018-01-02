# the compiler: gcc for C program, define as g++ for C++
CC = gcc

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  = -g -Wall -lm -lpthread

# the build target executable:
TARGET_CLI = client
TARGET_SER = server

all: client server

clean: 
	$(RM) $(TARGET_CLI) $(TARGET_SER)

server:	$(TARGET_SER)
	$(CC) $(CFLAGS) -o $(TARGET_SER) $(TARGET_SER).c

client:	$(TARGET_CLI)
	$(CC) $(CFLAGS) -o $(TARGET_CLI) $(TARGET_CLI).c
