/*
 ============================================================================
 Name        : radio_listener.c
 Author      : Chen Bary 302775333, Stas Karpanko 309427680
 ============================================================================
 */

#include <netinet/in.h>		//internet protocol family stuff
#include <arpa/inet.h>		//definitions for internet operations
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>

#define LISTEN_PORT 12701			// default port for TCP traffic
#define LISTEN_GROUP "127.0.0.1"	// default address for TCP: loopback interface
#define BUFFER 1024
#define maxIPlen 15

void *UDP(void *numStations);
void *TCP(void *numStations);
void error(char *msg);

int fdT, fdU, flag = 0, changeStation=0;  //flag to error function, changeStation to threads
unsigned char bufUDP[BUFFER]={0}, bufTCP[7]={0}, ipbuf[maxIPlen] = {0};			// buffer for UDP & TCP
struct sockaddr_in addrTCP, addrUDP;   	        // socket struct for UDP & TCP
struct ip_mreq mreq;
struct timeval tv;         					   // timeout for select function
int  n, rv, fdRead, nbytes = 0, sizeRead = sizeof(struct sockaddr_in), sizeAddr, runMidlleLoop=1;
uint16_t portNum;
fd_set readfds;          					   // group for select function

int main(void)
{
	//*********************** init TCP sockets ***********************//
	// set address & port for TCP socket
	bzero((char *) &addrTCP, sizeof(addrTCP));			// clear fdT mem space
	addrTCP.sin_family = AF_INET;						// set family - AF_INET
	addrTCP.sin_port = htons(LISTEN_PORT);			    // set port
	inet_aton(LISTEN_GROUP, &addrTCP.sin_addr);	// define IP address to loopback interface

	// Create a new socket - pointed to file descriptor fdT
	if ((fdT = socket(AF_INET,SOCK_STREAM,0)) < 0) 	// open TCP socket
		error("TCP socket creation error\n");
	flag = 1;
	// bind socket
	if (bind(fdT,(struct sockaddr *) &addrTCP,sizeof(addrTCP)) < 0)
		error("TCP socket bind error\n");
	//this is the start of the world - wait for UDP data from radio controler
	listen(fdT,5);
	if((fdRead = accept(fdT, (struct sockaddr*)&addrTCP, (socklen_t*)&sizeRead))<0)
		error("accept error\n");
	if((nbytes = recv(fdRead,bufTCP,sizeof(bufTCP),0))<0)	// listen on TCP port for first command
		error("error receiving command\n");

	// in bufTCP: < replyType , multicastGroup , portNumber >
	sprintf(ipbuf,"%d.%d.%d.%d",bufTCP[1], bufTCP[2], bufTCP[3], bufTCP[4]);	// get multicast address from buffer
	portNum = bufTCP[5]*256 + bufTCP[6];
	bufTCP[0] = 1;						// reply type set to 1
	if(send(fdRead,&bufTCP,7,0) == -1)		// send station information reply
		error("send error");
	memset(bufTCP,0,7);	// clear tcp buffer
	//******************** end init TCP sockets ***********************//

	while(1){	//start listen to UDP & TCP
		//*********************** init UDP sockets ***********************//
		if ((fdU = socket(AF_INET,SOCK_DGRAM,0)) < 0) 	// open UDP socket
			error("UDP socket creation error\n");
		flag = 2;
		bzero((char *) &addrUDP, sizeof(addrUDP));			// clear fdU memory space
		addrUDP.sin_family = AF_INET;						// set family - AF_INET
		addrUDP.sin_port = htons(portNum);			    	// set port
		addrUDP.sin_addr.s_addr = inet_addr((char*)ipbuf);	// define IP address to the multicast

		// bind socket
		if (bind(fdU,(struct sockaddr *) &addrUDP,sizeof(addrUDP)) < 0)
			error("UDP socket bind error\n");
		mreq.imr_multiaddr.s_addr = inet_addr((char*)ipbuf);
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		if(setsockopt(fdU, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))
			error("multicast connect error\n");

		runMidlleLoop = 1;
		//******************** end init UDP sockets ***********************//

		while(runMidlleLoop == 1){
			FD_ZERO(&readfds);			//clear select elements
			FD_SET(fdU, &readfds);		//add fdU to the elements
			FD_SET(fdRead, &readfds);   //add fdRead to the elements
			tv.tv_sec = 0;
			tv.tv_usec = 100000;  		//100 ms
			n = fdU+1;           		//last socket that were open + 1
			if((rv = select(n, &readfds, NULL, NULL, &tv)) == -1)
				error("select error\n");
			else if(rv == 0) //timeout
				continue;

			if(FD_ISSET(fdRead, &readfds)){ //TCP data from radio controller
				if((nbytes = recv(fdRead,bufTCP,sizeof(bufTCP),0)) < 0)	// Receive data from TCP port for first command
					error("error receiving command\n");
				if(nbytes == 0)
					error("error receiving command, nbytes=0\n");
				if(bufTCP[0] == 3)     //add that to close the radio_listener
					error("bye bye\n");
				sprintf(ipbuf,"%d.%d.%d.%d",bufTCP[1], bufTCP[2], bufTCP[3], bufTCP[4]);// get multicast address from buffer
				portNum = bufTCP[5]*256 + bufTCP[6];
				bufTCP[0] = 1;						// reply type set to 1
				if(send(fdRead,&bufTCP,7,0) == -1)	// send station information reply - ACK
					error("send message to controller error\n");
				memset(bufTCP,0,7);	// clear tcp buffer
				runMidlleLoop = 0;
			}

			if(FD_ISSET(fdU, &readfds)){ //UDP data
				sizeAddr = sizeof(addrUDP);
				if((nbytes = recvfrom(fdU, bufUDP, BUFFER, 0, (struct sockaddr*)&addrUDP, (socklen_t*)&sizeAddr))<0)
					error("receiving data error\n");
				write(STDOUT_FILENO, bufUDP, nbytes);
				memset(bufUDP, 0, nbytes);
			}
		}//while(1)

		if(setsockopt(fdU, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)))
			error("multicast disconnect error\n");
		close(fdU);
		flag = 1;
	}//while(1)

	error("bye bye\n");  //this is not error message, only the shut down the system
	return 0;
} // main

/*
 * This function print error message and close FD if it necessary
 * //flag = 1 TCP socet open, flag = 2 TCP and UDP sockets are open, flag = 0 TCP and UDP sockets are close
 */
void error(char *msg){
	printf("%s",msg);
	if(flag > 0)
		close(fdT);
	if(flag == 2)
		close(fdU);
	exit(EXIT_FAILURE);
}

void *UDP(void *numStations){

	return 0;
}
void *TCP(void *numStations){

	return 0;
}
