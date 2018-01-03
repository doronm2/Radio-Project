
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
#define 	OFF_INIT 		0
#define 	WAIT_WELCOME	1
#define 	ESTABLISHED  	2
#define 	WAIT_ANNOUNCE 	3
#define     UPLOAD_SONG     4
#define     WAIT_PERMIT     5

int state = 0;
pthread_t t;

typedef struct {
	uint8_t replyType;
	uint16_t numStations;
	uint32_t multicastGroup;
	uint16_t portNumber;
} Welcome_msg;

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
char songName[songNameSize]; */

Welcome_msg handShake(int TCP_sock);
static void listener(int UDP_sock, char* SERVER_IP,int SERVER_port);


// Input: SERVER's IP_address, SERVER's TCP WELCOME port.
//void TCP_receiver(char* SERVER_IP_address, short TCP_welcome_port)
int main(int argc, char* argv[])
{
	int TCP_client_Sock=0, UDP_sock=0, TCP_rec_bytes=0, TCP_pack_num =0 , TCP_pack_len=0;
	int SERVER_port, TCP_read_err=0;
	struct sockaddr_in serverAddr;
	char  buffer[Buffer_size+1], ask_song_msg [3] = {1,0,0} , songNameSize = 0 , user_input =0;
	//,songName[songNameSize];
	char * SERVER_IP , *up_song_msg;
	uint16_t stationNumber =0;
	uint32_t songSize = 0;
	Welcome_msg welcome_msg = {0};

	//Check that we got from the user all of the arguments to activate the server
	printf("input received: \nexe File:%s, Server IP address:%s,\nServer TCP_port num:%s,\n(total of %d args)\n", argv[0], argv[1], argv[2] , argc);
	if(argc != 3)
	{ printf("Too few/many arguments received. Bye bye!\n"); exit(1); }

	SERVER_IP = argv[1];
	SERVER_port = atoi(argv[2]);

	// Create the TCP&UDP socket and set it’s properties. The three arguments are:
	// 1) Internet domain. 2) Stream socket. 3) Default protocol (TCP in this case).
	TCP_client_Sock = socket(AF_INET, SOCK_STREAM, 0);
	if (TCP_client_Sock == -1)  { perror("Can't create TCP socket"); close(TCP_client_Sock); exit(1); }
	UDP_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (UDP_sock == -1) { perror("Can't create UDP socket"); close(UDP_sock); exit(1); }

	/*---- Configure settings of the server address struct ----*/
	serverAddr.sin_family = AF_INET; /* Address family = Internet */
	serverAddr.sin_port = htons(SERVER_port); 	/* Set destination TCP_port number, using htons function to use proper byte order */
	serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP); // set destination IP number - localhost, 127.0.0.1
	memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero); /* Set all bits of the padding field to 0 */

	/*---- Connect the socket to the server using the address struct ----*/
	if(connect(TCP_client_Sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr))== -1)
	{ perror("Can't connect to server"); close(TCP_client_Sock); exit(1); }

	welcome_msg = handShake(TCP_client_Sock);
	if(welcome_msg.numStations == 0)
		printf("error in welcome message! quitting program.");
	else
		printf("\nHandshake complete - connection established.\ndata received from server:\n %d stations, mGroup IP: %d, port: %d \nwaiting for user input! (chose a station int the range: 0-%d)",(uint16_t)welcome_msg.numStations,(uint32_t)welcome_msg.multicastGroup,(uint16_t)welcome_msg.portNumber,(uint16_t)welcome_msg.numStations);

	pthread_create(&t, NULL, listener, NULL);

	// Wait for socket to have data, Read socket data into buffer if there is Any data
	do // While (connection not closed by server or  - a realistic receiver doesn't know how many packets to expect
	{
		TCP_pack_len = recv(TCP_client_Sock, buffer, Buffer_size , 0);
		if(TCP_pack_len > 0)
		{
			printf("%s",buffer);
			TCP_pack_num++;
			TCP_rec_bytes += TCP_pack_len;
		}
		else if (TCP_pack_len < 0)
		{
			perror("Error in reading from socket");
			printf("\n");
			TCP_read_err++;
		}
		else
		{
			printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~~~Connection closed by server.~~~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
			break;
		}
	} while (TCP_pack_len != 0);


	// Free all resources;
	shutdown(UDP_sock, SHUT_RDWR);
	close(UDP_sock);

	// print what we received in the buffer and statistics.
	printf("\nStats: total of %d bytes received in TCP.\nTCP socket read errors: %d\n", TCP_rec_bytes,TCP_read_err);

	// Free all resources;
	if(shutdown(TCP_client_Sock, SHUT_RDWR)==-1)
	{ perror("Error in Shutting down Client socket"); close(TCP_client_Sock); exit(1); }

	//close the connection
	if(close(TCP_client_Sock) == -1 )
	{ perror("Error in closing client socket"); exit(1); }

	pthread_exit(NULL);
	return EXIT_SUCCESS;
}


Welcome_msg handShake(int TCP_sock)
{
	Welcome_msg welcome_msg = {0};
	int rec_bytes = 0;
	char hello_msg [3] = {0,0,0} , welc_buff [9] ;

	if(send(TCP_sock,hello_msg,sizeof(hello_msg),0) == -1)
	{ perror(""); printf("\nError in sending hello message"); return welcome_msg;}

	state = WAIT_WELCOME;

	rec_bytes = recv(TCP_sock, welc_buff, sizeof(welc_buff) , 0);
	if (rec_bytes == -1)
	{ perror(""); printf("\nError in sending hello message"); return welcome_msg;}
	else if (rec_bytes != 9)
	{ printf("\nError in welcome message - size incorrect"); return welcome_msg;}

	welcome_msg.replyType = (uint8_t)welc_buff[0];
	welcome_msg.numStations = (uint16_t)welc_buff[1];
	welcome_msg.multicastGroup = (uint32_t)welc_buff[3];
	welcome_msg.portNumber = (uint16_t)welc_buff[7];

	state = ESTABLISHED;
	return welcome_msg;
}

static void listener(int UDP_sock, char* SERVER_IP,int SERVER_port)
{
	struct ip_mreq mreq;
	int UDP_pack_len =0,UDP_pack_num =0,UDP_read_err=0,UDP_rec_bytes=0;
	struct sockaddr_in udp_serverAddr = {0};
	char  message[Buffer_size+1];
	socklen_t addr_len;



	//define the port and the multicast IP to send the data on
	udp_serverAddr.sin_family = AF_INET;
	udp_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	udp_serverAddr.sin_port = htons(SERVER_port);
	addr_len = sizeof udp_serverAddr;

	//try to bind to the the UDP socket of the multicast Group
		if(bind(UDP_sock, (struct sockaddr *) &udp_serverAddr, sizeof(udp_serverAddr))==-1)
		{ perror("Can't bind UDP socket"); close(UDP_sock); exit(1); }

		mreq.imr_multiaddr.s_addr = inet_addr(SERVER_IP);
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		setsockopt(UDP_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)); //join mGroup

		printf("\nListening for multicast messages..\n");

		// Wait for socket to have data, Read socket data into buffer if there is Any data
		while(1) // While (not all data received || connection closed by server)
		{
			UDP_pack_len = recvfrom(UDP_sock, message, sizeof(message), 0, (struct sockaddr *) &udp_serverAddr, &addr_len);
			if(UDP_pack_len > 0)
			{
				printf("%s \n",message);
				UDP_pack_num++;
				UDP_rec_bytes += UDP_pack_len;
			}
			else if (UDP_pack_len < 0)
			{
				perror("Error in reading from socket");
				printf("\n");
				UDP_read_err++;
			}
			else //if (pack_len == 0) - Connection closed by server
			{
				printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~~~Connection closed by server.~~~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
				break;
			}
		}

		setsockopt(UDP_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)); //DROP mGroup MEMBERSHIP

		// print what we received in the buffer and statistics.
		printf("\nStats: total of %d bytes received in UDP.\nUDP socket read errors: %d\n", UDP_rec_bytes,UDP_read_err);
}
