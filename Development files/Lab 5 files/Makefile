# the compiler: gcc for C program, define as g++ for C++
CC = gcc

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  = -g -Wall

# the build target executable:
TARGET_UR = UDP_receiver
TARGET_US = UDP_sender
TARGET_TR = TCP_receiver
TARGET_TS = TCP_sender

all: udp tcp

clean: clean_tcp clean_udp 

clean_tcp:
	$(RM) $(TARGET_TR) $(TARGET_TS)

clean_udp:	
	$(RM) $(TARGET_UR) $(TARGET_US)

tcp:  clean_tcp tcp_sender tcp_receiver

udp:  clean_udp udp_receiver udp_sender 

tcp_sender:	$(TARGET_TS)
$(TARGET_TS): $(TARGET_TS).c
	$(CC) $(CFLAGS) -o $(TARGET_TS) $(TARGET_TS).c

tcp_receiver:	$(TARGET_TR)
$(TARGET_TR): $(TARGET_TR).c
	$(CC) $(CFLAGS) -o $(TARGET_TR) $(TARGET_TR).c

udp_sender:	$(TARGET_US)
$(TARGET_US): $(TARGET_US).c
	$(CC) $(CFLAGS) -o $(TARGET_US) $(TARGET_US).c

udp_receiver:	$(TARGET_UR)
$(TARGET_UR): $(TARGET_UR).c
	$(CC) $(CFLAGS) -o $(TARGET_UR) $(TARGET_UR).c
