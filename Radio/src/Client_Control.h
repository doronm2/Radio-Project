/*
 * Client_Control.h
 *
 *  Created on: Jan 3, 2018
 *      Author: ubu
 */


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>

#define Buffer_size 	1024
//state defenitions
#define 	OFF_INIT 				0
#define 	WAIT_WELCOME			1
#define 	ESTABLISHED  			2
#define 	WAIT_ANNOUNCE 			3
#define     UPLOAD_SONG   			4
#define     WAIT_PERMIT   			5

#define 	maxIPlen 15


//global variables
int state = 0;
int NumStations;
int change_station =0;
char * mGroup_IP;
pthread_t t;

typedef struct {
	uint8_t replyType;
	uint16_t numStations;
	char multicastGroup[maxIPlen];
	uint16_t portNumber;
} Welcome_msg;


typedef struct {
	int tcp_sock;
	char* mGroup_IP;
	int mGroup_port;
} sock_data;

Welcome_msg handShake(int TCP_sock);
void* listener(void* udp_sock_data); //int UDP_sock, char* SERVER_IP,int SERVER_port)
int handle_user_input(char* user_input, int TCP_Sock);
sock_data setTCP_sock(char* serv_ip, int serv_port);
void handle_TCP_and_IO(int TCP_Sock);
void sendinvalid();




							// client to server messages
/* 	Hello msg:
uint8_t commandType = 0;
uint16_t reserved = 0;

	AskSong msg:
uint8_t commandType = 1;
uint16_t stationNumber;

	UpSong msg:
uint8_t commandType = 2;
uint32_t songSize; //in bytes
uint8_t songNameSize;
char songName[songNameSize];

							// server to client messages
 	 Welcome:
uint8_t replyType = 0;
uint16_t numStations;
uint32_t multicastGroup;
uint16_t portNumber;

	Announce:
uint8_t replyType = 1;
uint8_t songNameSize;
char songName[b];

	PermitSong:
uint8_t replyType = 2;
uint8_t permit;

	InvalidCommand:
uint8_t replyType = 3;
uint8_t replyStringSize;
char replyString[replyStringSize];

	NewStations:
uint8_t replyType = 4;
uint16_t newStationNumber;
 */
