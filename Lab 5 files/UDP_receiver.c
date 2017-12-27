/* UDP_receiver
Author1: Victor Martinov,
ID: 307835249
Author2: Doron Maman,
ID: 302745146
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>

#define Buffer_size 	1024

/****************** UDP client code ****************/

// Input: SERVER's IP_address, SERVER's TCP port, The number of parts of data that need to be received;
//void TCP_receiver(char* IP_address, short port,int num_parts)
int main(int argc, char* argv[])
{
	int sock=0, rec_bytes=0, packet_num =0 , pack_len =0,port=0 ,parts=0,read_err=0;
	char * IP;
	struct sockaddr_in serverAddr = {0};
	struct ip_mreq mreq;
	char  message[Buffer_size+1];
	socklen_t addr_len;


	//Check that we got from the user all of the arguments to activate the server
	printf("input received: \nexe File:%s, Server IP address:%s,\nServer port num:%s, Num of parts:%s,\n(total of %d args)\n", argv[0], argv[1], argv[2], argv[3] , argc);
	if(argc != 4) { printf("too few arguments. bye bye!\n"); exit(1); }

	IP = argv[1];
	port = atoi(argv[2]);
	parts = atoi(argv[3]);

	//try to open a socket to send the data
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) { perror("Can't create socket"); close(sock); exit(1); }

	//define the port and the multicast IP to send the data on
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddr.sin_port = htons(port);
	addr_len = sizeof serverAddr;

	//try to bind to the the UDP socket of the multicast Group
	if(bind(sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr))==-1)
	{ perror("Can't bind UDP socket"); close(sock); exit(1); }

	mreq.imr_multiaddr.s_addr = inet_addr(IP);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)); //join mGroup

	printf("\nListening for multicast messages..\n");

	// Wait for socket to have data, Read socket data into buffer if there is Any data
	while(packet_num < parts) // While (not all data received || connection closed by server)
	{
		pack_len = recvfrom(sock, message, sizeof(message), 0, (struct sockaddr *) &serverAddr, &addr_len);
		if(pack_len > 0)
		{
			printf("%s \n",message);
			packet_num++;
			rec_bytes += pack_len;
		}
		else if (pack_len < 0)
		{
			perror("Error in reading from socket");
			printf("\n");
			read_err++;
		}
		else //if (pack_len == 0) - Connection closed by server
		{
			printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~~~Connection closed by server.~~~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
			break;
		}
	}
	setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)); //DROP mGroup MEMBERSHIP

	// print what we received in the buffer and statistics.
	printf("\nStats: total of %d bytes of %d expected bytes, received.\nsocket read errors: %d\n", rec_bytes, Buffer_size*parts,read_err);

	// Free all resources;
	shutdown(sock, SHUT_RDWR);
	close(sock);

	return EXIT_SUCCESS;
}


