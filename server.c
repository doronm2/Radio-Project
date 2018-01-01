/* TCP_receiver
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

typedef struct{
    int num_of_station;
        

};
/****************** TCP client code ****************/

// Input: SERVER's IP_address, SERVER's TCP port, The number of parts of data that need to be received;
//void TCP_receiver(char* IP_address, short port,int num_parts)
int main(int argc, char* argv[])
{
	int TCP_client_Sock=0, rec_bytes=0, packet_num =0 , pack_len=0, parts, port, read_err=0;
	struct sockaddr_in serverAddr;
	char buffer[Buffer_size+1];
	char * IP;

	//Check that we got from the user all of the arguments to activate the server
	printf("input received: \nexe File:%s, Server IP address:%s,\nServer port num:%s, Num of parts:%s,\n(total of %d args)\n", argv[0], argv[1], argv[2], argv[3] , argc);
	if(argc != 4)
	{ printf("Too few/many arguments received. Bye bye!\n"); exit(1); }

	IP=argv[1];
	port=atoi(argv[2]);
	parts=atoi(argv[3]);

	// Create the socket and set it’s properties. The three arguments are:
	// 1) Internet domain. 2) Stream socket. 3) Default protocol (TCP in this case).
	TCP_client_Sock = socket(AF_INET, SOCK_STREAM, 0);
	if (TCP_client_Sock == -1)  { perror("Can't create socket"); close(TCP_client_Sock); exit(1); }

	/*---- Configure settings of the server address struct ----*/
	serverAddr.sin_family = AF_INET; /* Address family = Internet */
	serverAddr.sin_port = htons(port); 	/* Set destination port number, using htons function to use proper byte order */
	serverAddr.sin_addr.s_addr = inet_addr(IP); // set destination IP number - localhost, 127.0.0.1
	memset(serverAddr.sin_zero, '\0', sizeof serverAddr.sin_zero); /* Set all bits of the padding field to 0 */

	/*---- Connect the socket to the server using the address struct ----*/
	if(connect(TCP_client_Sock, (struct sockaddr *) &serverAddr, sizeof(serverAddr))== -1)
	{ perror("Can't connect to server"); close(TCP_client_Sock); exit(1); }

	// Wait for socket to have data, Read socket data into buffer if there is Any data
	printf("info received:\n\n");
	while(packet_num < parts) // While (connection not closed by server) - a realistic receiver doesn't know how many packets to expect
	{
		pack_len = recv(TCP_client_Sock, buffer, Buffer_size , 0);
		if(pack_len > 0)
		{
			printf("%s",buffer);
			packet_num++;
			rec_bytes += pack_len;
		}
		else if (pack_len < 0)
		{
			perror("Error in reading from socket");
			printf("\n");
			read_err++;
		}
		else
		{
			printf("\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n~~~~Connection closed by server.~~~~\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
			break;
		}
	}

	// print what we received in the buffer and statistics.
	printf("\nStats: total of %d bytes of %d expected bytes, received.\nsocket read errors: %d\n", rec_bytes, Buffer_size*parts,read_err);

	// Free all resources;
	if(shutdown(TCP_client_Sock, SHUT_RDWR)==-1)
	{ perror("Error in Shutting down Client socket"); close(TCP_client_Sock); exit(1); }

	//close the connection
	if(close(TCP_client_Sock) == -1 )
	{ perror("Error in closing client socket"); exit(1); }

	return EXIT_SUCCESS;
}
